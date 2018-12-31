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
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.Set;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.ObjIntConsumer;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import spartan.Spartan;
import spartan.SpartanBase;
import spartan.annotations.ChildWorkerCommand;
import spartan.annotations.SupervisorMain;
import spartan.util.io.ReadLine;

@SuppressWarnings({"JavaDoc", "unused"})
public class App extends SpartanBase {
  private static final String clsName = App.class.getName();
  private static final String watchdogRetryErrMsgFmt =
          "Watchdog monitor still attempting restart of %s child worker process";
  private static final String childProcessRetryErrMsg =
          "Child process error detected - will retry after back-off interval pause%n%s%n";
  private static final String resetBackoffToken = "\nRESET_BACKOFF_INDEX\n";
  private static final Pattern reset_backoff_regex = Pattern.compile("^RESET_BACKOFF_INDEX");
  private static final int[] PSEUDO_FIBONACCI = new int[] { 3, 3, 3, 5, 5, 8, 13, 21, 34, 55, 89, 144, 233, 377 };
  private static final AtomicInteger workerThreadNbr = new AtomicInteger(1);
  private static final ExecutorService workerThread = Executors.newSingleThreadExecutor(r -> {
    final Thread t = new Thread(r);
    t.setDaemon(true);
    t.setName(format("%s-worker-thread-#%d", programName, workerThreadNbr.getAndIncrement()));
    return t;
  });
  private static final Set<Integer> _pids = ConcurrentHashMap.newKeySet(53);
  private static final AtomicBoolean quitFlag = new AtomicBoolean(false);
  private static final AtomicReference<Future<?>> atomicCallFuture = new AtomicReference<>(null);

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
    log(LL_INFO, () -> format("%s: hello world - supervisor service has started!%n", programName));

    final ObjIntConsumer<ExecutorService> waitOnExecServiceTermination = (excSrvc, waitTime) -> {
      // waits for termination for waitTime seconds only
      for (int i = waitTime; !excSrvc.isTerminated() && i > 0; i--) {
        try {
          excSrvc.awaitTermination(1, TimeUnit.SECONDS);
        } catch (InterruptedException ignored) {
        }
        System.out.print('.');
      }
      excSrvc.shutdownNow();
    };

    final Thread shutdownHandler = new Thread(() -> {
      quitFlag.set(true);
      final Future<?> callFuture = atomicCallFuture.getAndSet(null);
      workerThread.shutdown();
      waitOnExecServiceTermination.accept(workerThread, 30);
      if (callFuture != null) {
        handleWorkerTaskFuture(callFuture);
      }
    });
    Runtime.getRuntime().addShutdownHook(shutdownHandler);

    final Runnable watchDogTask = () -> invokeChildCmd("continuous_etl", "-run-forever", "dummy.json.gz");
    final Thread t = new Thread(watchDogTask);
    t.setDaemon(true);
    t.setName(format("%s-watchdog-thread-#%d", programName, workerThreadNbr.getAndIncrement()));
    t.start();

    enterSupervisorMode(_pids);

    log(LL_INFO, () -> format("%s exiting normally", programName));
  }

  /**
   * This SpartanBase method is being overridden because this example
   * <i>supervisor</i> will track pids of child workers that it invokes itself
   * in its own <i>_pids</i> collection. So when a child worker process
   * completes, its pid needs to be removed from said <i>_pids</i> collection.
   * (The <i>supervisor</i> will add the child worker's pid to the collection
   *  when it is invoked.)
   *
   * <p/><b>NOTE:</b> It is essential to invoke the super method.
   *
   * @param pid the process pid of a child worker process that has terminated
   */
  @Override
  public void childProcessCompletionNotify(int pid) {
    super.childProcessCompletionNotify(pid);
    _pids.remove(pid);
  }

  private static void handleWorkerTaskFuture(Future<?> callFuture) {
    try {
      callFuture.get(); // waits on callable worker task to complete (but it should really run indefinitely)
    } catch (InterruptedException|CancellationException ignored) {
    } catch (Throwable e) {
      log(LL_ERR, () -> format(childProcessRetryErrMsg, e));
    }
  }

  /**
   * Illustrates invoking a child worker process and presiding as a watch dog
   * supervisor, monitoring the child process output, and restarting if needed.
   *
   * <p/>The implementation will illustrate use of Spartan.invokeCommand() to execute
   * the child worker. It also illustrates associating the output stream of the
   * child worker process with an asynchronous task, which consumes it.
   *
   * <p/>The asynchronous task will consume output from the child worker process and
   * write it to the original response stream as a simple echoing operation.
   *
   * @param args the second element is the child command to be executed, followed
   *             by options (if any)
   */
  private static void invokeChildCmd(String... args) {
    assert(args.length > 0);
    try {
      final int maxIndex = PSEUDO_FIBONACCI.length - 1;
      final AtomicInteger atomicIndex = new AtomicInteger(0);
      final String childCommand = args[0].toLowerCase();
      final Matcher reset_backoff = reset_backoff_regex.matcher("");

      for(int index = atomicIndex.get(); !quitFlag.get() ;
          index = atomicIndex.updateAndGet(i -> i < maxIndex ? i + 1 : maxIndex))
      {
        if (index > 5) {
          log(LL_INFO, () -> format(watchdogRetryErrMsgFmt, childCommand));
        }

        // Asynchronously consume output from the invoked command and examine it for health status
        atomicCallFuture.set(workerThread.submit(() -> {
          try {
            final InvokeResponse rtn = Spartan.invokeCommand(args);
            _pids.add(rtn.childPID);
            final Thread currThrd = Thread.currentThread();
            currThrd.setName(format("%s - child process (pid:%d)", currThrd.getName(), rtn.childPID));
            try (final Reader rdr = new InputStreamReader(rtn.inStream)) {
              final ReadLine lineRdr = new ReadLine(rdr, 1024);
              CharSequence line;
              while ((line = lineRdr.readLine()) != null) {
                if (line.length() <= 0) continue;
                // check to see if the line is the reset-backoff token
                if (reset_backoff.reset(line).find()) {
                  atomicIndex.set(0); // reset back-off index
                }
//                System.out.println(line);
              }
            }
          } catch (Throwable e) {
            uncheckedExceptionThrow(e);
          }
        }));

        // use of atomic reference insures only one thread has ownership access to this Future
        final Future<?> callFuture = atomicCallFuture.getAndSet(null);
        if (callFuture != null) {
          handleWorkerTaskFuture(callFuture);
        } else {
          // if reach here then shutdown handler has been invoked so wait for executor to orderly terminate
          //noinspection StatementWithEmptyBody
          while (!workerThread.awaitTermination(3, TimeUnit.SECONDS));
          return; // exiting program now
        }

        if (quitFlag.get()) {
          break;
        } else {
          // wait for back-off interval before retrying
          Thread.sleep(PSEUDO_FIBONACCI[index] * 1000);
        }
      }
    } catch (InterruptedException ignored) {
    }
  }

  /**
   * Diagnostic helper method that prints debug info for a called command method.
   * @param rspStream output stream to print info to
   * @param methodName name of the command method that was called
   * @param args arguments that were passed to the invoked method
   */
  private static void print_method_call_info(PrintStream rspStream, String methodName, String[] args) {
    final String stringizedArgs = String.join("\" \"", args);
    rspStream.printf(">> %s.%s(\"%s\")%n", clsName, methodName, stringizedArgs);
  }

  /**
   * Example Spartan child worker entry-point method.
   * (Does a simulated processing activity.)
   *
   * <p/>This example illustrates using Spartan technique to
   * allow only one child process to execute this command
   * at any give time (i.e., singleton execution semantics).
   *
   * <p/>The annotation declares it is invoked via the command
   * CONTINUOUS_ETL.
   * <p/>
   * The annotation also supplies Java JVM heap size options.
   *
   * @param args command line arguments passed to the child worker
   *        (first argument is the name of the command invoked)
   * @param rspStream the invoked operation can write results
   *        (and/or health check status) to the invoker
   */
  @ChildWorkerCommand(cmd="CONTINUOUS_ETL", jvmArgs={"-Xms128m", "-Xmx324m"})
  public static void doContinuousEtlProcessing(String[] args, PrintStream rspStream) {
    final String methodName = "doContinuousEtlProcessing";
    try (final PrintStream rsp = rspStream) {
      assert(args.length > 0);
      print_method_call_info(rspStream, methodName, args);

      final String cmd_lc = args[0].toLowerCase();
      final String pidFileBaseName = String.join("-", "spartan-watchdog-ex", cmd_lc);

      if (Spartan.isFirstInstance(pidFileBaseName)) {

        final ScheduledExecutorService scheduler = Executors.newScheduledThreadPool(1);
        scheduler.scheduleAtFixedRate(() -> rsp.print(resetBackoffToken), 15, 15, TimeUnit.SECONDS);

        doSimulatedEtlProcessing(args, rsp);

      } else {
        final String errmsg = format("Child command %s is already running", cmd_lc);
        rsp.printf("WARNING: %s%n", errmsg);
        log(LL_WARN, errmsg::toString);
      }
    } catch (Throwable e) {
      e.printStackTrace(rspStream);
      uncheckedExceptionThrow(e);
    }
  }

  /**
   * Just a helper method that does something and prints it to the supplied response output stream
   * for the sake or illustration purpoeses in example programs. Calls on existing test code that
   * is in the Spartan.jar library.
   *
   * <p/><b>NOTE:</b> The presence of <i>-run-forever</i> option will cause it to run perpetually.
   *
   * @param args arguments being passed to the test code command
   * @param rsp response output stream that is written to by the command's implementation
   * @throws ClassNotFoundException
   * @throws NoSuchMethodException
   * @throws SecurityException
   * @throws IllegalAccessException
   * @throws IllegalArgumentException
   * @throws InvocationTargetException
   */
  private static void doSimulatedEtlProcessing(String[] args, final PrintStream rsp)
      throws ClassNotFoundException, NoSuchMethodException, SecurityException, IllegalAccessException,
      IllegalArgumentException, InvocationTargetException
  {
    final Class<?> testSpartanCls = Class.forName("TestSpartan");
    final Method childWorkerDoCommand = testSpartanCls.getMethod("childWorkerDoCommand", String[].class,
        PrintStream.class);
    // invoke Spartan built-in test code to simulate doing some processing
    // (writes messages to the response stream at random intervals)
    args[0] = "genesis";
    childWorkerDoCommand.invoke(null, args, rsp);
  }
}
