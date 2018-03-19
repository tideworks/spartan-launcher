/* TestSpartan.java

Copyright 2015 - 2017 Tideworks Technology
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
import static java.lang.String.format;
import static spartan.Spartan.invokeCommand;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.PrintStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.util.concurrent.ConcurrentHashMap;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Set;

public final class TestSpartan extends spartan.SpartanBase {
  private static final String clsName = TestSpartan.class.getSimpleName();
  private static final String runForeverOptn = "-run-forever";
  private static final Set<Integer> _pids = ConcurrentHashMap.newKeySet(53);

  private static enum WhichChildCommand {
    CDC {
      @Override
      public boolean dispatchOnCommand(String[] args, PrintStream rspStream) { return false; }
    },
    GENESIS {
      @Override
      public boolean dispatchOnCommand(String[] args, PrintStream rspStream) {
        if (args.length > 1) {
          int decrement_count = 0;
          boolean runForever = false;
          if (args.length > 2) {
            for (int i = 0; i < args.length; i++) {
              final String arg = args[i];
              if (arg.compareToIgnoreCase(runForeverOptn) == 0) {
                runForever = true;
                args[i] = null;
                decrement_count++;
                break;
              }
            }
          }
          args[0] = null;
          decrement_count++;
          final String[] remain_args = new String[args.length - decrement_count];
          for (int i = 0, j = 0; i < args.length; i++) {
            final String arg = args[i];
            if (arg != null) {
              remain_args[j++] = arg;
            }
          }
          final String input_string = format("\"%s\"", String.join("\" \"", remain_args));
          initiateProcessGenesis(input_string, rspStream, runForever);
        } else {
          final String errmsg = format("%s child worker has no input JSON filepath specified to process", this);
          rspStream.printf("ERROR: %s%n", errmsg);
          log(LL_ERR, ()->errmsg);
        }
        return true;
      }
    };
    public abstract boolean dispatchOnCommand(String[] args, PrintStream rspStream);
  }

  private static enum WhichSupervisorCommand {
    INVOKECHILDCMD {
      @Override
      public boolean dispatchOnCommand(String[] args, PrintStream rspStream) {
        supervisorInvokeChildCommand(Arrays.copyOfRange(args, 1, args.length), rspStream);
        return true;
      }
    };
    public abstract boolean dispatchOnCommand(String[] args, PrintStream rspStream);
  }

  public static void main(String[] args) {
    final String methodName = "main";
    log(LL_INFO, ()->format(">> %s.%s(args count:%d)", clsName, methodName, args.length));

    if (args.length > 1) {
      try {
        final int count = Integer.parseInt(args[1]);
        launchChildProcesses(count);
      } catch(NumberFormatException ex) {
        log(LL_WARN, ()->format("%s.%s(): number of child processes to start is expected for second argument: %s",
                                clsName, methodName, args[1]));
      } catch(Throwable ex) {
        final StringWriter swr = new StringWriter(512);
        final PrintWriter  pwr = new PrintWriter(swr);
        pwr.printf("%s.%s(): failure launching child processes:%n", clsName, methodName);
        ex.printStackTrace(pwr);
        log(LL_ERR, ()->swr.toString());
      }
    }

    log(LL_INFO, ()->format("<< %s.%s()", clsName, methodName));
  }

  private static void launchChildProcesses(final int count)
      throws ClassNotFoundException, InvokeCommandException, KillProcessGroupException
  {
    final String methodName = "launchChildProcesses";
    try {
      final ArrayList<Thread>  healthCheckThrds = new ArrayList<>(count);

      for(int i = 1; i <= count; i++) {
        final InvokeResponse rsp = invokeCommand("genesis", format("test-file.%03d.json.gz", i), runForeverOptn);
        _pids.add(rsp.childPID);
        log(LL_DEBUG, ()->format("processing output from '%s' child process (pid:%d)", clsName, rsp.childPID));
        final Thread thrd = processChildProcessResponseAsync(rsp);
        healthCheckThrds.add(thrd);
        thrd.start();
      }

      // blocks indefinitely here - when service shuts down then
      // an OS signal will cause program to unblock and exit
      try {
        _lock.lock();
        _condition.await();
      } catch (InterruptedException e) {
      } finally {
        _lock.unlock();
      }
    } catch (InterruptedException e) {
    } finally {
      log(LL_DEBUG, ()->format("%s.%s() - unblocked", clsName, methodName));
      drainKillProcessGroup(_pids);
    }
  }

  private static Thread processChildProcessResponseAsync(final InvokeResponse rsp) {
    final Thread thrd = new Thread(() -> {
      try {
        try(final BufferedReader rdr = new BufferedReader(new InputStreamReader(rsp.inStream))) {
          @SuppressWarnings("unused")
          String line;
          while((line = rdr.readLine()) != null) {
            // TODO - need to do health check monitoring here
            //System.out.println(line);
          }
        }
      } catch(Throwable ex) {
        final StringWriter swr = new StringWriter(512);
        final PrintWriter  pwr = new PrintWriter(swr);
        pwr.printf("%s exception so exiting thread:%n", Thread.currentThread().getName());
        ex.printStackTrace(pwr);
        log(LL_ERR, ()->swr.toString());
      }
    });
    thrd.setName(format("'%s' child process (pid:%d)-%s", clsName, rsp.childPID, thrd.getName()));
    thrd.setDaemon(true);
    return thrd;
  }

  @Override
  public void childProcessCompletionNotify(int pid) {
    log(LL_DEBUG, ()->format("%s.childProcessCompletionNotify(pid:%d)", clsName, pid));
    super.childProcessCompletionNotify(pid);
    _pids.remove(pid);
  }

  @Override
  public void supervisorDoCommand(String[] args, java.io.PrintStream rspStream) {
    final String methodName = "supervisorDoCommand";
    log(LL_DEBUG, ()->format(">> %s.%s(args count:%d)", clsName, methodName, args.length));

    try {
      // invoke the helper command dispatch method in SpartanBase
      coreDispatch(args, rspStream, "supervisor",
                   (String cmdStr) -> WhichSupervisorCommand.valueOf(cmdStr),
                   (WhichSupervisorCommand cmdObj, String[] a, PrintStream rs) -> cmdObj.dispatchOnCommand(a, rs));
    } catch(CommandDispatchException ex) {
      rspStream.printf("%s: %s%n", ex.getClass().getSimpleName(), ex.getMessage());
      final StringWriter swr = new StringWriter(512);
      ex.printStackTrace(new PrintWriter(swr));
      log(LL_ERR, ()->swr.toString());
    }

    log(LL_DEBUG, ()->format("<< %s.%s()", clsName, methodName));
  }

  private static void supervisorInvokeChildCommand(String[] args, PrintStream rspStream) {
    try {
      final InvokeResponse rsp = invokeCommand(args);
      _pids.add(rsp.childPID);
      // detached thread will consume the response output of the invoked child process
      final Thread thrd = processChildProcessResponseAsync(rsp, rspStream);
      thrd.start();
    } catch(Throwable e) {
      try (final PrintStream rspStrm = rspStream) { // only closing the response stream if an exception was caught
        rspStrm.printf("ERROR: supervisor failed invoking child command: %s%n", args[0]);
        e.printStackTrace(rspStrm);
      }
    }
  }

  private static Thread processChildProcessResponseAsync(final InvokeResponse rsp, final PrintStream rspStream) {
    final Thread thrd = new Thread(() -> {
      try (final PrintStream rspStrm = rspStream) {
        try(final BufferedReader rdr = new BufferedReader(new InputStreamReader(rsp.inStream))) {
          String line;
          while((line = rdr.readLine()) != null) {
            rspStrm.println(line);
          }
        }
      } catch(Throwable e) {
        rspStream.printf("%s exception so exiting thread:%n", Thread.currentThread().getName());
        e.printStackTrace(rspStream);
      }
    });
    thrd.setName(format("'%s' child process (pid:%d)-%s", clsName, rsp.childPID, thrd.getName()));
    thrd.setDaemon(true);
    return thrd;
  }

  public static void childWorkerDoCommand(String[] args, PrintStream rspStream) {
    final String methodName = "childWorkerDoCommand";
    log(LL_DEBUG, ()->format(">> %s.%s(args count:%d)", clsName, methodName, args.length));

    final Thread currThrd = Thread.currentThread();

    Runtime.getRuntime().addShutdownHook(new Thread(() -> {
      log(LL_DEBUG, ()->format(">> %s.%s() - addShutdownHook() invoked%n", clsName, methodName));
      currThrd.interrupt();
      try {
        currThrd.join(3000); // wait up to 3 secs for exiting out of this child process
      } catch(InterruptedException e) {}
      log(LL_DEBUG, ()->format("<< %s.%s() - addShutdownHook()%n", clsName, methodName));
     }));

    try {
      // invoke the helper command dispatch method in SpartanBase
      coreDispatch(args, rspStream, "Child worker",
                   (String cmdStr) -> WhichChildCommand.valueOf(cmdStr),
                   (WhichChildCommand cmdObj, String[] a, PrintStream rs) -> cmdObj.dispatchOnCommand(a, rs));
    } catch(CommandDispatchException ex) {
      rspStream.printf("%s: %s%n", ex.getClass().getSimpleName(), ex.getMessage());
      final StringWriter swr = new StringWriter(512);
      ex.printStackTrace(new PrintWriter(swr));
      log(LL_ERR, ()->swr.toString());
    }

    log(LL_DEBUG, ()->format("<< %s.%s()%n", clsName, methodName));
  }

  private static void initiateProcessGenesis(final String inputFilePath, PrintStream rspStream, boolean runForever) {
    final String msg = format("%s.initiateProcessGenesis(%s)", clsName, inputFilePath);
    log(LL_DEBUG, ()->msg);

    // test code - writes some text data to response stream and closes it
    try(final PrintStream rsp = rspStream) {
      rsp.printf("DEBUG: %s%n", msg);
      rsp.flush();
      final java.util.Random rnd = new java.util.Random();
      for(int i = 0; runForever || i < 30;) {
        int n = rnd.nextInt(1000);
        if (n <= 0) {
          n = 100;
        }
        try {
          Thread.sleep(n);
        } catch(InterruptedException e) {
          break;
        }
        rsp.printf("DEBUG: test message #%d - duration %d ms%n", i, n);
        if (++i < 0) {
          i = 0; // set back to a non negative integer (wrapping happens if running in forever mode)
        }
      }
    }
  }
}
