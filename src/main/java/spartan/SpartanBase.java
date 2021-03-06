/* SpartanBase.java

Copyright 2015 - 2018 Tideworks Technology
Author: Roger D. Voss

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*/
package spartan;

import static java.lang.String.format;
import static java.util.Comparator.comparing;
import static java.util.stream.Collectors.toList;
import static spartan.Spartan.killProcessGroupSIGINT;

import java.io.Closeable;
import java.io.IOException;
import java.io.InputStream;
import java.io.PrintStream;
import java.lang.reflect.Method;
import java.time.LocalDateTime;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.locks.Condition;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;
import java.util.function.Function;
import java.util.function.Supplier;
import java.util.stream.Stream;

@SuppressWarnings({"WeakerAccess", "unused"})
public class SpartanBase implements Spartan {
  private   static final String clsName = SpartanBase.class.getSimpleName();
  protected static final Lock _lock = new ReentrantLock();
  protected static final Condition _condition = _lock.newCondition();
  protected static final LockedAccessCondition _service_condition = new LockedAccessCondition(_lock, _condition);
  protected static final ConcurrentHashMap<Integer, ChildProcess> _childProcesses = new ConcurrentHashMap<>(53);
  protected static int loggingLevel = LL_INFO;
  protected static String programName;
  protected static ThreadContext mainMethodThreadContext;

  protected static class ChildProcess {
    public final int pid;
    public final String commandLine;
    public final LocalDateTime timeStamp;
    public ChildProcess(int pid, String commandLine) {
      this.pid = pid;
      this.commandLine = commandLine;
      timeStamp = LocalDateTime.now();
    }
    public Integer getPid() { return pid; }
    public String toString() {
      return format("%s pid:%d '%s'", timeStamp, pid, commandLine);
    }
  }

  protected static final class LockedAccessCondition {
    private boolean isSignaled = false;
    private final Lock lock;
    private final Condition condition;
    public LockedAccessCondition() {
      this(new ReentrantLock());
    }
    private LockedAccessCondition(Lock lock) {
      this(lock, lock.newCondition());
    }
    public LockedAccessCondition(Lock lock, Condition condition) {
      this.lock = lock;
      this.condition = condition;
    }
    public void reset() {
      try {
        lock.lock();
        isSignaled = false;
      } finally {
        lock.unlock();
      }
    }
    public void await() {
      try {
        lock.lock();
        while(!isSignaled) {
          condition.await();
        }
      } catch (InterruptedException e) {
        isSignaled = true;
      } finally {
        lock.unlock();
      }
    }
    public <X extends Exception> void awaitWithReset(int millisecs, Action<X> action) throws X {
      try {
        lock.lock();
        action.apply();
        while(!isSignaled) {
          condition.await(millisecs, TimeUnit.MILLISECONDS);
        }
        isSignaled = false;
      } catch (InterruptedException ignored) {
      } finally {
        lock.unlock();
      }
    }
    @SuppressWarnings("UnusedReturnValue")
    public boolean signalAll() {
      if (lock.tryLock()) try {
        condition.signalAll();
        return isSignaled = true;
      } finally {
        lock.unlock();
      }
      return false;
    }
    public <X extends Exception> boolean signalAll(Action<X> action) throws X {
      if (lock.tryLock()) try {
        action.apply();
        condition.signalAll();
        return isSignaled = true;
      } finally {
        lock.unlock();
      }
      return false;
    }
  }

  @FunctionalInterface
  public interface Action<X extends Exception> {
    void apply() throws X;
  }

  @FunctionalInterface
  public interface DispatchCmd<T> {
    boolean getAsBoolean(final T cmd, final String[] args, final PrintStream rspStream);
  }

  @SuppressWarnings("serial")
  public static final class CommandDispatchException extends Exception {
    public CommandDispatchException(String message) { super(message); }
    public CommandDispatchException(String message, Throwable cause) { super(message, cause); }
  }

  protected static void log(int level, Supplier<String> msg) {
    if (level < loggingLevel) return;
    Spartan.log(level, msg.get());
  }

  @Override
  public String getProgramName() { return programName; }

  @Override
  public void supervisorShutdown() {
    final String methodName = "supervisorShutdown";
    log(LL_DEBUG, ()->format(">> %s.%s() - invoked", clsName, methodName));
    _service_condition.signalAll();
    try {
      mainMethodThreadContext.interrupt(); // in case blocked in a low-level sys call like read(fd)
    } catch (Throwable ignored) {}
    log(LL_DEBUG, ()->format("<< %s.%s()", clsName, methodName));
  }

  @SuppressWarnings("unused")
  private static void main(String progName, int logLevel, Method targetMain, String[] args) {
    programName  = progName;
    loggingLevel = logLevel;
    final String methodName = "main";
    log(LL_DEBUG, ()->format(">> %s.%s(args count:%d)", clsName, methodName, args.length));

    mainMethodThreadContext = ThreadContext.getCurrentThreadContext();

    try {
      targetMain.invoke(targetMain, (Object)args);

      // Will block indefinitely here if the target main() had no service listener, etc;
      // when service shuts down then an OS signal will cause program to unblock and exit
      _service_condition.await();
    } catch(RuntimeException ex) {
      final Throwable cause = ex.getCause();
      final Throwable e = cause != null ? cause : ex;
      log(LL_ERR, ()->format("%s.%s(): failure executing %s:", clsName, methodName, targetMain.getName()));
      if (LL_ERR >= loggingLevel) {
        e.printStackTrace(System.err);
      }
    } catch(Throwable ex) {
      log(LL_ERR, ()->format("%s.%s(): failure executing %s:", clsName, methodName, targetMain.getName()));
      if (LL_ERR >= loggingLevel) {
        ex.printStackTrace(System.err);
      }
    }

    log(LL_DEBUG, ()->format("<< %s.%s()", clsName, methodName));
  }

  /**
   * Blocks indefinitely here - when service shuts down then
   * an OS signal will cause program to unblock and exit.
   * <p>
   * <i>Will be able to respond to supervisor commands when in this mode.</i>
   *
   * @param pids  collection of pids of child processes that are active; these
   *              will be cleaned up in finally statement when unblocked.
   */
  protected static void enterSupervisorMode(final Collection<Integer> pids) {
    // Blocks indefinitely here - when service shuts down then
    // an OS signal will cause program to unblock and exit.
    // (Will be able to respond to supervisor commands, such as GENESIS.)
    try {
      _service_condition.await();
    } finally {
      drainKillProcessGroup(pids);
    }
  }

  protected static void enterSupervisorMode() { enterSupervisorMode(null); }

  protected static void drainKillProcessGroup(final Collection<Integer> pids) {
    final String methodName = "drainKillProcessGroup";
    final Set<Integer> all_pids = new HashSet<>(_childProcesses.keySet());
    if (pids != null && pids.size() > 0) {
      all_pids.addAll(pids);
    }
    if (all_pids.size() > 0) {
      int i = 1;
      try {
        do {
          @SuppressWarnings("UnusedAssignment")
          int childpid = 0;
          final Optional<Integer> pid = _childProcesses.keySet()
              .stream()
              .filter(all_pids::remove)
              .findFirst();
          if (!pid.isPresent()) break; // when can't find a process group pid amongst active pids, then break out
          childpid = pid.get();
          if (childpid > 0) {
            try {
              final int n = i++;
              final int chldpid = childpid;
              log(LL_DEBUG, () -> format("%s.%s(): calling [%d] killProcessGroupSIGINT(pid:%d)",
                      clsName, methodName, n, chldpid));
              _childProcesses.remove(chldpid);
              killProcessGroupSIGINT(chldpid); // signal the process group of child processes to cease and exit
            } catch (KillProcessException|KillProcessGroupException ignored) {}
          }
        }
        while (all_pids.size() > 0);
      } catch(java.util.NoSuchElementException ignored) {}
    }
  }

  protected static Stream<String> statusHelper() {
    final Function<ChildProcess, Integer> byPID = ChildProcess::getPid;
    final List<String> entries = _childProcesses.values().stream()
      .sorted(comparing(byPID))
      .map(v -> format("%24s   %12s   %s", v.timeStamp, v.pid, v.commandLine))
      .collect(toList());
    final String header = format("%n%24s | %12s | %s", "   *** timestamp ***   ", "*** pid ***", "*** command-line ***");
    final String footer = entries.size() > 0 ?
      format("%d child processes active%n", entries.size()) : format("%nNo child processes currently active%n");
    return Stream.concat(Stream.concat(Stream.of(header), entries.stream()), Stream.of(footer));
  }

  @Override
  public void status(PrintStream statusRspStream) {
    try(final PrintStream rsp = statusRspStream) {
      statusHelper().forEach(rsp::println);
      rsp.flush();
    }
  }

  @Override
  public void childProcessNotify(int pid, String commandLine) {
    _childProcesses.put(pid, new ChildProcess(pid, commandLine));
  }

  @Override
  public void childProcessCompletionNotify(int pid) {
    _childProcesses.remove(pid);
  }

  private static void unknownCommandDefaultResponse(String[] args, java.io.PrintStream rspStream, String cmdType) {
    try(final PrintStream rsp = rspStream) {
      final String errmsg = args.length > 0 ?
            format("ERROR: %s command \'%s\' not implemented", cmdType, args[0])
          : format("ERROR: %s has no command to execute", cmdType);
      rsp.println(errmsg);
    }
  }

  @Override
  public void supervisorDoCommand(String[] args, java.io.PrintStream rspStream) {
    unknownCommandDefaultResponse(args, rspStream, "supervisor");
  }

  @SuppressWarnings("unused")
  private static void childWorkerDoCommand(String[] args, PrintStream rspStream) {
    unknownCommandDefaultResponse(args, rspStream, "child worker");
  }

  protected static <T> void coreDispatch(final String[] args, final PrintStream rspStream, final String desc,
      final Function<String,T> makeCmd, final DispatchCmd<T> dispatchCmd) throws CommandDispatchException
  {
    PrintStream rsp = rspStream;
    try {
      if (args.length <= 0) {
        final String errmsg = format("%s has no command to execute", desc);
        rsp.println("ERROR: " + errmsg);
        throw new CommandDispatchException(errmsg);
      } else {
        final T whichCmd = makeCmd.apply(args[0].toUpperCase());
        rsp = null; // the callee now takes responsibility for the response stream - to close it when done
        if (!dispatchCmd.getAsBoolean(whichCmd, args, rspStream)) {
          rsp = rspStream;
          final String errmsg = format("%s - command \'%s\' not implemented", desc, args[0]);
          rsp.println("ERROR: " + errmsg);
          throw new CommandDispatchException(errmsg);
        }
      }
    } catch(IllegalArgumentException|NullPointerException ex) {
      rsp = rspStream;
      final String errmsg = format("%s - command \'%s\' not recognized", desc, args[0]);
      rsp.println("ERROR: " + errmsg);
      throw new CommandDispatchException(errmsg);

    } finally {
      if (rsp != null) {
        rsp.close();
      }
    }
  }

  /**
   * Diagnostic helper method that prints debug info for a called command method.
   *
   * @param outS stream to print info to
   * @param clsName owning class of the command method that was called
   * @param methodName name of the command method that was called
   * @param args arguments that were passed to the invoked method
   */
  public static void print_method_call_info(PrintStream outS, String clsName, String methodName, String[] args) {
    final String output = String.join("\" \"", args);
    outS.printf(">> invoked %s.%s(\\\"%s\\\")%n", clsName, methodName, output);
  }

  /**
   * Helper method for closing {@link java.io} stream objects. If
   * calling {@link Closeable#close()} throws an exception, it is
   * caught and logged to {@link System#err}, so is intended to be
   * used in the outermost code prior to returning from a sub-command
   * and to where the stream arguments can't be assumed to be in a
   * valid state for use.
   * <p>
   * <b>NOTE:</b> It is safe to pass a null reference to this method,
   * will detect and return, doing nothing.
   *
   * @param ioStream the {@link java.io} stream object to be closed
   * @param desc descriptive text that will be included in error logging
   */
  public static void closeStream(Closeable ioStream, String desc) {
    if (ioStream != null) {
      try {
        ioStream.close();
      } catch (IOException e) {
        final String errmsg = String.format("exception on closing pipe %s:", desc);
        log(LL_ERR, errmsg::toString);
        e.printStackTrace(System.err);
      }
    }
  }

  /**
   * A callable supervisor command does not return any result.
   */
  @FunctionalInterface
  protected interface CallableSupervisorCommand {
    void call(String cmd, String[] args, PrintStream outStream, PrintStream errStream, InputStream inStream) throws
          Exception;
  }

  /**
   * Helper method that does the outermost boilerplate handling for
   * supervisor commands - the user method to be executed to implement
   * the command is passed as the right-most lambda argument.
   * <p>
   * <b>NOTE:</b> This helper method will be responsible for closing
   * the stream arguments - the called callable does not have to close
   * them.
   * <p>
   * If stream argument ownership needs to be transferred to a detached
   * task, then use {@link #commandForwarderNoStreamClose(String[],
   * PrintStream, PrintStream, InputStream, CallableSupervisorCommand)}
   * instead.
   *
   * @param args supervisor command arguments (first element is name of the command)
   * @param outStream output stream to write processing data/info to
   * @param errStream error output stream to write errors, health-check-info, out-of-band protocol
   * @param inStream an input stream for receiving data from the invoker
   * @param callable the user-supplied callable which implements the command
   */
  protected static void commandForwarder(String[] args, PrintStream outStream, PrintStream errStream,
                                         InputStream inStream, CallableSupervisorCommand callable)
  {
    try {
      assert args.length > 0;
      final String cmd = args[0];
      final String[] remainingArgs = args.length > 1 ? Arrays.copyOfRange(args, 1, args.length) : new String[0];

      callable.call(cmd, remainingArgs, outStream, errStream, inStream);

    } catch(Throwable e) {
      errStream.printf("%nERROR: %s: exception thrown:%n", args.length > 0 ? args[0] : "{invalid command}");
      e.printStackTrace(errStream);
    } finally {
      closeStream(outStream, "output stream");
      closeStream(errStream, "error output stream");
      closeStream(inStream, "input stream");
    }
  }

  /**
   * Helper method that does the outermost boilerplate handling for
   * supervisor commands (but it does not close the io stream arguments)
   * - the user method to be executed to implement the command is passed
   * as the right-most lambda argument.
   * <p>
   * Frequently supervisor commands may want to transfer ownership of
   * the streams to a detached task, in which case the transferred streams
   * should not be closed. This helper method is best for that scenario.
   *
   * @param args supervisor command arguments (first element is name of the command)
   * @param outStream output stream to write processing data/info to
   * @param errStream error output stream to write errors, health-check-info, out-of-band protocol
   * @param inStream an input stream for receiving data from the invoker
   * @param callable the user-supplied callable which implements the command
   */
  protected static void commandForwarderNoStreamClose(String[] args, PrintStream outStream, PrintStream errStream,
                                                      InputStream inStream, CallableSupervisorCommand callable)
  {
    try {
      assert args.length > 0;
      final String cmd = args[0];
      final String[] remainingArgs = args.length > 1 ? Arrays.copyOfRange(args, 1, args.length) : new String[0];

      callable.call(cmd, remainingArgs, outStream, errStream, inStream);

    } catch(Throwable e) {
      errStream.printf("%nERROR: %s: exception thrown:%n", args.length > 0 ? args[0] : "{invalid command}");
      e.printStackTrace(errStream);
    }
  }

  /**
   * A callable worker child process command must return a status code
   * result where 0 indicates success and 1 indicates failure.
   */
  @FunctionalInterface
  public interface CallableWorkerCommand {
    int call(String cmd, String[] args, PrintStream outStream, PrintStream errStream, InputStream inStream) throws
          Exception;
  }

  /**
   * Helper method that does the outermost boilerplate handling for
   * worker child commands - the user method to be executed to implement
   * the command is passed as the right-most lambda argument.
   * <p>
   * <b>NOTE:</b> This helper method will be responsible for closing
   * the stream arguments - the called callable does not have to close
   * them.
   *
   * @param args worker child process command arguments (first element is name of the command)
   * @param outStream worker's output stream to write processing data/info to
   * @param errStream worker's error output stream to write errors, health-check-info, out-of-band protocol
   * @param inStream an input stream for the worker to receive data from the invoker
   * @param callable the user-supplied callable which implements the command
   */
  public static void commandForwarder(String[] args, PrintStream outStream, PrintStream errStream,
                                         InputStream inStream, CallableWorkerCommand callable)
  {
    @SuppressWarnings("UnusedAssignment")
    int status_code = 0;

    try {
      assert args.length > 0;
      final String cmd = args[0];
      final String[] remainingArgs = args.length > 1 ? Arrays.copyOfRange(args, 1, args.length) : new String[0];

      status_code = callable.call(cmd, remainingArgs, outStream, errStream, inStream);

    } catch(Throwable e) {
      errStream.printf("%nERROR: %s: exception thrown:%n", args.length > 0 ? args[0] : "{invalid command}");
      e.printStackTrace(errStream);
      status_code = 1;
    } finally {
      closeStream(outStream, "output stream");
      closeStream(errStream, "error output stream");
      closeStream(inStream, "input stream");
    }

    System.exit(status_code); // worker child process should return a meaningful status code indicating success/failure
  }
}