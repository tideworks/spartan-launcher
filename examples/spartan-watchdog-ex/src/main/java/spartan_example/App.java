/* App.java

Copyright 2018 Tideworks Technology
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
package spartan_example;

import static java.lang.String.format;

import java.io.InputStreamReader;
import java.io.PrintStream;
import java.io.Reader;
import java.util.Set;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.BooleanSupplier;
import java.util.function.Consumer;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import spartan.Spartan;
import spartan.SpartanBase;
import spartan.annotations.ChildWorkerCommand;
import spartan.annotations.SupervisorCommand;
import spartan.annotations.SupervisorMain;
import spartan.util.io.ReadLine;

@SuppressWarnings({"JavaDoc", "unused", "WeakerAccess"})
public class App extends SpartanBase {
  private static final String clsName = App.class.getName();
  private static final String watchdogRetryErrMsgFmt =
          "Watchdog monitor still attempting restart of %s child worker process";
  private static final String childProcessRetryErrMsg =
          "Child process error detected - will retry after back-off interval pause%n%s%n";
  private static final String resetBackoffToken = "\nRESET_BACKOFF_INDEX\n";
  private static final Pattern reset_backoff_regex = Pattern.compile("^RESET_BACKOFF_INDEX");
  private static final int[] PSEUDO_FIBONACCI = new int[] { 3, 3, 3, 5, 5, 8, 13, 21, 34, 55, 89, 144, 233, 377 };
  private static final Set<Integer> _pids = ConcurrentHashMap.newKeySet(53);
  private static final AtomicReference<App> spartanSingletonInstance = new AtomicReference<>(null);

  // watchdog service instance member fields
  private final AtomicInteger atomicChildPID;
  private final AtomicReference<Future<?>> workerTaskAtomicCallFuture;
  private final AtomicReference<Future<?>> watchdogTaskAtomicCallFuture;
  private final ExecutorService watchdogExecutor;
  private final ExecutorService workerExecutor;
  private final Thread shutdownHandler;

  // watchdog service instance initialization (will run as a singleton object managed by Spartan runtime)
  {
    final App app = spartanSingletonInstance.get();
    if (app != null) {
      // constructing from pre-existing singleton object instance
      atomicChildPID = app.atomicChildPID;
      workerTaskAtomicCallFuture = app.workerTaskAtomicCallFuture;
      watchdogTaskAtomicCallFuture = app.watchdogTaskAtomicCallFuture;
      watchdogExecutor = app.watchdogExecutor;
      workerExecutor = app.workerExecutor;
      shutdownHandler = app.shutdownHandler;
    } else {
      // constructing brand-new singleton object instance
      atomicChildPID = new AtomicInteger(0);
      workerTaskAtomicCallFuture = new AtomicReference<>(null);
      watchdogTaskAtomicCallFuture = new AtomicReference<>(null);

      final AtomicInteger threadNumber = new AtomicInteger(1);

      this.watchdogExecutor = Executors.newSingleThreadExecutor(r -> {
        final Thread t = new Thread(r);
        t.setDaemon(true);
        t.setName(format("%s-watchdog-thread-#%d", programName, threadNumber.getAndIncrement()));
        return t;
      });

      this.workerExecutor = Executors.newSingleThreadExecutor(r -> {
        final Thread t = new Thread(r);
        t.setDaemon(true);
        t.setName(format("%s-worker-thread-#%d", programName, threadNumber.getAndIncrement()));
        return t;
      });

      // A service shutdown handler (will respond to SIGINT/SIGTERM signals and Spartan stop command);
      // basically in this example program it just manages the ExecutorService thread pools for shutdown
      shutdownHandler = new Thread(() -> {
        signalChildProcessToQuit();
        final Future<?> callFuture = workerTaskAtomicCallFuture.getAndSet(null);
        workerExecutor.shutdown();
        waitOnExecServiceTermination(workerExecutor, 5); // will await up to 5 seconds
        watchdogExecutor.shutdown();
        waitOnExecServiceTermination(watchdogExecutor, 5); // will await up to 5 seconds
        if (callFuture != null) {
          handleWorkerTaskFuture(callFuture);
        }
      });
      Runtime.getRuntime().addShutdownHook(shutdownHandler);

      spartanSingletonInstance.set(this);
    }
  }

  private void signalChildProcessToQuit() {
    final int childPID = atomicChildPID.getAndSet(0);
    if (childPID > 0) {
      try {
        Spartan.killSIGTERM(childPID);
      } catch (KillProcessException ignore) {}
    }
  }

  @SuppressWarnings("SameParameterValue")
  private static void waitOnExecServiceTermination(ExecutorService excSrvc, int waitTime) {
    // waits for termination for waitTime (seconds only)
    for (int i = waitTime; !excSrvc.isTerminated() && i > 0; i--) {
      try {
        excSrvc.awaitTermination(1, TimeUnit.SECONDS);
      } catch (InterruptedException ignored) {
      }
      System.out.print('.');
    }
    excSrvc.shutdownNow();
  }

  /**
   * Designated Spartan service entry-point method. This will
   * be the <i>supervisor</i> Java JVM process.
   *
   * <p/>Java JVM heap options for the <i>supervisor</i> process
   * should be specified in the example application's Spartan
   * config.ini file.
   *
   * @param args options to be processed for the service
   *             initialization. (The -service option will
   *             be one of them as is required by Spartan.)
   */
  @SupervisorMain
  public static void main(String[] args) {
    log(LL_INFO, "hello world - supervisor service has started!"::toString);

    final App app = new App();

    app.startWatchdog(); // initiate watchdog task

    enterSupervisorMode(_pids);

    log(LL_INFO, "exiting normally"::toString);
  }

  /**
   * This {@link SpartanBase} method is being overridden because this example
   * <i>supervisor</i> will track pids of child workers that it invokes itself
   * in its own {@link #_pids} collection. So when a child worker process
   * completes, its pid needs to be removed from said <i>_pids</i> collection.
   * (The <i>supervisor</i> will add the child worker's pid to the collection
   *  when it is invoked - this overridden method will remove it when the child
   *  process completes.)
   * <p>
   * The <i>_pids</i> collection is shown being passed to {@link #enterSupervisorMode()}
   * as seen above in {@link #main(String[])}; when the <i>supervisor</i> service
   * shuts down, any child process pids that remain active will be sent a SIGTERM
   * signal to cause them to exit.
   * <p>
   * <b>NOTE:</b> It is essential to invoke the <i>super</i> method!
   *
   * @param pid the process pid of a child worker process that has terminated
   */
  @Override
  public void childProcessCompletionNotify(int pid) {
    super.childProcessCompletionNotify(pid);
    _pids.remove(pid);
  }

  // core helper method for starting the watchdog task
  private void startWatchdog() {
    // here we start a single detached thread that will be the watchdog (watching over a worker child process)
    final Runnable watchDogTask = () -> invokeChildCmd("continuous_etl", "-run-forever", "dummy.json.gz");
    watchdogTaskAtomicCallFuture.set(watchdogExecutor.submit(watchDogTask));
  }

  // core helper method for stopping the watchdog task
  private void initiateStopping() {
    final Consumer<AtomicReference<Future<?>>> stopper = (anAtomicCallFuture) -> {
      final Future<?> callFuture = anAtomicCallFuture.getAndSet(null);
      if (callFuture != null) {
        callFuture.cancel(true);
        handleWorkerTaskFuture(callFuture);
      }
    };

    App app = spartanSingletonInstance.getAndSet(this); // insures Spartan-managed App instance is THE instance
    if (app == null) {
      app = this;
    }
    app.signalChildProcessToQuit();
    stopper.accept(app.workerTaskAtomicCallFuture);
    stopper.accept(app.watchdogTaskAtomicCallFuture);
  }

  @SupervisorCommand("START_WATCHDOG")
  public void startWatchdog(String[] args, PrintStream rspStream) {
    final String methodName = "startWatchdog";

    try (final PrintStream rsp = rspStream) {
      print_method_call_info(rsp, methodName, args);
      assert args.length > 0;

      final String cmd = args[0];

      if (atomicChildPID.get() > 0) {
        rsp.printf("%s: INFO: %s: service watchdog already running%n", programName, cmd);
      } else {
        rsp.printf("%s: INFO: %s: preparing to start service watchdog...%n", programName, cmd);

        // first make sure is truly stopped before starting it again
        initiateStopping();

        // here we start a single detached thread that will be the watchdog (watching over a worker child process)
        startWatchdog();

        rsp.printf("%s: INFO: %s: service watchdog started%n", programName, cmd);
      }
    } catch(Throwable e) {
      rspStream.printf("%nERROR: %s: exception thrown:%n", (args.length > 0 ? args[0] : "{invalid command}"));
      e.printStackTrace(rspStream);
    }
  }

  @SupervisorCommand("STOP_WATCHDOG")
  public void stopWatchdog(String[] args, PrintStream rspStream) {
    final String methodName = "stopWatchdog";

    try (final PrintStream rsp = rspStream) {
      print_method_call_info(rsp, methodName, args);
      assert args.length > 0;

      final String cmd = args[0];

      rsp.printf("%s: INFO: %s: stopping service watchdog...%n", programName, cmd);

      initiateStopping();

      rsp.printf("%s: INFO: %s: service watchdog stopped%n", programName, cmd);

    } catch(Throwable e) {
      rspStream.printf("%nERROR: %s: exception thrown:%n", (args.length > 0 ? args[0] : "{invalid command}"));
      e.printStackTrace(rspStream);
    }
  }

  /**
   * Illustrates invoking a child worker process and presiding as a watch dog
   * supervisor, monitoring the child process output, and restarting if needed.
   * <p>
   * The implementation illustrates use of {@link Spartan#invokeCommand(String...)}
   * to execute the child worker. It also illustrates associating the output stream
   * of the child worker process with an asynchronous task, which consumes it.
   * <p>
   * The asynchronous task will consume output from the child worker process and
   * write it to the original response stream as a simple echoing operation.
   *
   * @param args the second element is the child command to be executed, followed
   *             by options (if any)
   */
  private void invokeChildCmd(String... args) {
    assert(args.length > 0);
    try {
      final String childCommand = args[0].toLowerCase();
      final int maxIndex = PSEUDO_FIBONACCI.length - 1;
      final AtomicInteger atomicIndex = new AtomicInteger(0);
      final Matcher reset_backoff = reset_backoff_regex.matcher("");
      final BooleanSupplier returnTrue = () -> true;
      final BooleanSupplier isChildPID = () -> atomicChildPID.get() > 0;
      final AtomicReference<BooleanSupplier> continueFlag = new AtomicReference<>(returnTrue);

      for(int index = atomicIndex.get(); continueFlag.get().getAsBoolean();
          index = atomicIndex.updateAndGet(i -> i < maxIndex ? i + 1 : maxIndex))
      {
        if (index > 5) {
          log(LL_INFO, () -> format(watchdogRetryErrMsgFmt, childCommand));
        }

        // Asynchronously consume output from the invoked sub-command and examine it for health status
        workerTaskAtomicCallFuture.set(workerExecutor.submit(() -> {
          try {
            final InvokeResponse rsp = Spartan.invokeCommand(args); // invoke the child worker process
            _pids.add(rsp.childPID);
            atomicChildPID.set(rsp.childPID);
            continueFlag.set(isChildPID);

            final Thread currThrd = Thread.currentThread();
            currThrd.setName(format("%s - child process (pid:%d)", currThrd.getName(), rsp.childPID));

            try (final Reader rdr = new InputStreamReader(rsp.inStream)) {
              final ReadLine lineRdr = new ReadLine(rdr, 1024); // ReadLine is a Spartan io utility class

              CharSequence line;
              while ((line = lineRdr.readLine()) != null) {
                if (line.length() <= 0) continue;
                // check to see if the line is the reset-backoff token
                if (reset_backoff.reset(line).find()) {
                  atomicIndex.set(0); // reset back-off index
                }
//                System.out.println(line); // when uncommented will echo child process output to service's stdout
              }
            }
          } catch (Throwable e) {
            uncheckedExceptionThrow(e);
          }
        }));

        // use of AtomicReference insures only one thread has ownership access to this task Future
        final Future<?> callFuture = workerTaskAtomicCallFuture.getAndSet(null);
        if (callFuture != null) {
          handleWorkerTaskFuture(callFuture);
        } else {
          // if reach here then shutdown handler has been invoked so wait for executor to orderly terminate
          //noinspection StatementWithEmptyBody
          while (!workerExecutor.awaitTermination(3, TimeUnit.SECONDS));
          return; // exiting program now
        }

        if (atomicChildPID.get() > 0) {
          // wait for back-off interval before retrying
          Thread.sleep(PSEUDO_FIBONACCI[index] * 1000);
        } else {
          break;
        }
      }
    } catch (InterruptedException ignored) {}
  }

  /**
   * Exceptions derived from {@link Throwable}, especially {@link Exception} derived
   * classes, are re-thrown as unchecked.
   *
   * <p/><i>Rethrown exceptions are not wrapped yet the compiler does not detect them as
   * checked exceptions at compile time so they do not need to be declared in the lexically
   * containing method signature.</i>
   *
   * @param t an exception derived from java.lang.Throwable that will be re-thrown
   * @return R generic argument allows for use in return statements (though will throw
   *           exception and thus never actually return)
   * @throws T the re-thrown exception
   */
  @SuppressWarnings("all")
  private static <T extends Throwable, R> R uncheckedExceptionThrow(Throwable t) throws T { throw (T) t; }

  private static void handleWorkerTaskFuture(Future<?> callFuture) {
    Throwable ex = null;
    try {
      callFuture.get(); // waits on callable worker task to complete (but it should really run indefinitely)
    } catch (InterruptedException|CancellationException ignored) {
    } catch (ExecutionException e) {
      ex = e.getCause() != null ? e.getCause() : e;
    } catch (Throwable e) {
      ex = e;
    }
    if (ex != null) {
      final Throwable e = ex;
      log(LL_ERR, () -> format(childProcessRetryErrMsg, e));
    }
  }

  /**
   * Diagnostic helper method that prints debug info for a called command method.
   *
   * @param rspStream output stream to print info to
   * @param methodName name of the command method that was called
   * @param args arguments that were passed to the invoked method
   */
  private static void print_method_call_info(PrintStream rspStream, String methodName, String[] args) {
    final String stringizedArgs = String.join("\" \"", args);
    rspStream.printf(">> %s.%s(\"%s\")%n", clsName, methodName, stringizedArgs);
  }

  /**
   * Example Spartan child worker sub-command entry-point method.
   * (Does a simulated processing activity.)
   * <p>
   * This example illustrates using Spartan technique to
   * allow only one child process to execute this command
   * at any give time (i.e., <i>singleton execution semantics</i>).
   * <p>
   * The annotation declares it is invoked via the command
   * CONTINUOUS_ETL. When invoked from a shell command line,
   * the sub-command name is case-insensitive.
   * <p>
   * The annotation also supplies Java JVM heap size options.
   * <p>
   * <b>NOTE:</b> a child worker command entry-point method must
   * be declared static.
   *
   * @param args command line arguments passed to the child worker
   *        (first argument is the name of the command invoked)
   * @param rspStream the invoked operation can write results
   *                  (and/or health check status) to the invoker
   */
  @ChildWorkerCommand(cmd="CONTINUOUS_ETL", jvmArgs={"-Xms48m", "-Xmx128m"})
  public static void doContinuousEtlProcessing(String[] args, PrintStream rspStream) {
    final String methodName = "doContinuousEtlProcessing";
    assert args.length > 0;

    final String cmd = args[0];

    int status_code = 0;

    try (final PrintStream rsp = rspStream) {
      print_method_call_info(rspStream, methodName, args);

      final String cmd_lc = args[0].toLowerCase();
      final String pidFileBaseName = String.join("-", "spartan-watchdog-ex", cmd_lc);

      if (Spartan.isFirstInstance(pidFileBaseName)) {

        final ScheduledExecutorService scheduler = Executors.newScheduledThreadPool(1);
        scheduler.scheduleAtFixedRate(() -> rsp.print(resetBackoffToken), 15, 15, TimeUnit.SECONDS);

        spartan.test.invokeGenerateDummyTestOutput(args, rsp, rsp);

      } else {
        final String errmsg = format("Child command %s is already running", cmd_lc);
        rsp.printf("WARNING: %s%n", errmsg);
        log(LL_WARN, errmsg::toString); // logs to the service's stderr
      }
    } catch (Throwable e) { // catch all exceptions here and deal with them (don't let them propagate)
      rspStream.printf("%nERROR: %s: exception thrown:%n", cmd);
      e.printStackTrace(rspStream);
      status_code = 1;
    }

    System.exit(status_code);
  }
}