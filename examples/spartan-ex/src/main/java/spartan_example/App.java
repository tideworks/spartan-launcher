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

import java.io.*;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

import spartan.Spartan;
import spartan.SpartanBase;
import spartan.annotations.ChildWorkerCommand;
import spartan.annotations.SupervisorCommand;
import spartan.annotations.SupervisorMain;
import spartan.util.io.ReadLine;

@SuppressWarnings({"JavaDoc", "unused"})
public class App extends SpartanBase {
  private static final String clsName = App.class.getName();
  private static final Set<Integer> _pids = ConcurrentHashMap.newKeySet(53);
  private final ExecutorService workerExecutor;

  // instance initialization (will run as a singleton object managed by Spartan)
  {
    final AtomicInteger threadNumber = new AtomicInteger(1);

    workerExecutor = Executors.newCachedThreadPool(r -> {
      final Thread t = new Thread(r);
      t.setDaemon(true);
      t.setName(String.format("%s-pool-thread-#%d", clsName, threadNumber.getAndIncrement()));
      return t;
    });

    // A service shutdown handler (will respond to SIGINT/SIGTERM signals and Spartan stop command);
    // basically in this example program it just manages the ExecutorService thread pool for shutdown
    final Thread shutdownHandler = new Thread(() -> {
      workerExecutor.shutdown();
      waitOnExecServiceTermination(workerExecutor, 5); // will await up to 5 seconds
    });
    Runtime.getRuntime().addShutdownHook(shutdownHandler);
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
   * <b>config.ini</b> file.
   *
   * @param args options to be processed for the service
   *             initialization. (The -service option will
   *             be one of them as is required by Spartan.)
   */
  @SupervisorMain
  public static void main(String[] args) {
    log(LL_INFO, "hello world - supervisor service has started!"::toString);

    // TODO: do one time service initialization here

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

  /**
   * Overriding status() method so can augment it with a little
   * bit more information (default implementation list out any
   * child processes that are active - will add some JVM info).
   *
   * @param statusRspStream stream where status output is written to
   */
  @Override
  public void status(PrintStream statusRspStream) {
    try (final PrintStream rsp = statusRspStream) {
      statusHelper().forEach(rsp::println);
      final Runtime rt = Runtime.getRuntime();
      final int OneMegaByte = 1024 * 1024;
      rsp.printf(
            "Supervisor JVM:%n  total memory: %12d MB%n   free memory: %12d MB%n   used memory: %12d MB%n    max memory: %12d MB%n",
            rt.totalMemory() / OneMegaByte,
            rt.freeMemory() / OneMegaByte,
            (rt.totalMemory() - rt.freeMemory()) / OneMegaByte,
            rt.maxMemory() / OneMegaByte
      );
      rsp.println();
      rsp.flush();
    }
  }

  /**
   * An example <i>supervisor</i> sub-command entry-point method. This method will
   * be invoked on a thread running in the <i>supervisor</i> Java JVM process.
   * <p>
   * The implementation generates the Fibonacci Sequence. An upper ceiling can
   * be passed as an argument.
   * <p>
   * <b>NOTE:</b> A supervisor sub-command implementation should never call the
   * JDK {@link System#exit(int)} method (as illustrated per the child sub-command
   * examples below) as that would result in exiting the supervisor service!
   * <p>
   * <b>NOTE:</b> A supervisor sub-command entry-point method must an instance method.
   *
   * @param args command line arguments passed to the <i>supervisor</i> process
   *             (first argument is the name of the sub-command invoked)
   * @param rspStream the generated results are written to this response stream
   */
  @SupervisorCommand("GENFIB")
  public void generateFibonacciSequence(String[] args, PrintStream rspStream) {
    print_method_call_info(rspStream, clsName, "generateFibonacciSequence", args);

    try (final PrintStream rsp = rspStream) {
      assert args.length > 0;

      final double maxCeiling = args.length > 1 ? Double.parseDouble(args[1]) : 30 /* default */;

      int count = 0;
      double j = 0, i = 1;
      rsp.print(j);
      rsp.println();
      count++;
      if (maxCeiling <= j) return;
      rsp.print(i);
      rsp.println();
      count++;
      if (maxCeiling == i) return;
      for(;;) {
        double tmp = i;
        i += j;
        j = tmp;
        if (i > maxCeiling) break;
        rsp.print(i);
        rsp.println();
        count++;
      }

      rsp.printf("%ngenerated %d values%n", count);

    } catch(Throwable e) {
      rspStream.printf("%nERROR: %s: exception thrown:%n", (args.length > 0 ? args[0] : "{invalid command}"));
      e.printStackTrace(rspStream);
    }
  }

  /**
   * An example <i>supervisor</i> sub-command entry-point method that serves as
   * a utility mechanism to get the <i>supervisor</i> process itself to invoke
   * a specified child worker sub-command, where the supervisor process takes
   * ownership of consuming the child process processing output stream.
   * <p>
   * The implementation will illustrate use of {@link Spartan#invokeCommandEx(String...)}
   * to execute the child process worker. It also illustrates associating the output
   * stream of the child worker process with an asynchronous task, which consumes it.
   * <p>
   * The asynchronous task will consume output from the child worker process and
   * write it to the original response stream as a simple echoing operation. (This
   * is just an example for purpose of showing different possible techniques. Real
   * programs would likely apply processing logic to the child output stream.)
   * <p>
   * Because the task thread takes ownership of the output streams back to the invoker
   * of the supervisor sub-command, it's okay that the supervisor sub-command go
   * ahead and return back to the invoker - the task thread will execute in detached
   * manner because it belongs to a thread pool executor that has the lifetime of
   * the service (it's a member field of {@link App} class, which is derived from
   * {@link SpartanBase}).
   * <p>
   * <b>NOTE:</b> A supervisor sub-command implementation should never call the
   * JDK {@link System#exit(int)} method (as illustrated per the child sub-command
   * examples below) as that would result in exiting the supervisor service!
   * <p>
   * <b>NOTE:</b> A supervisor sub-command entry-point method must an instance method.
   *
   * @param args the second element is the child sub-command to be executed, followed
   *             by options (if any)
   * @param outStream the invoked operation can write processing results
   *                  (and/or health check status) to the invoker
   * @param errStream stream for writing error information to the invoker
   * @param inStream stream from receiving input from invoker
   */
  @SupervisorCommand("INVOKECHILDCMD")
  public void invokeChildCmd(String[] args, PrintStream outStream, PrintStream errStream, InputStream inStream) {
    print_method_call_info(outStream, clsName, "invokeChildCmd", args);
    commandForwarderNoStreamClose(args, outStream, errStream, inStream, this::coreInvokeChildCmd);
  }

  private void coreInvokeChildCmd(String cmd, String[] args, PrintStream outStream, PrintStream errStream,
                                  InputStream inStream)
  {
    try {
      if (args.length < 1) {
        errStream.println("ERROR: no child command specified - insufficient command line arguments");
        return;
      }

      final String child_cmd = args[0];

      if (cmd.equalsIgnoreCase(child_cmd)) {
        errStream.printf("ERROR: cannot invoke self, %s, as child command to run%n", child_cmd);
        return;
      }

      // spawning a worker child process per the specified child sub-command
      final InvokeResponseEx rsp = Spartan.invokeCommandEx(args);
      _pids.add(rsp.childPID);

      final PrintStream taskOutS = outStream; // the async task will take ownership of the output stream
      outStream = null; // null it out so that the outer try-finally block no longer will close it
      final PrintStream taskErrS = errStream; // the async task will take ownership of the error output stream

      // Asynchronously consumes output from the invoked sub-command process and writes it to the response stream
      final Runnable detachedTask = () -> {
        //
        // Our example code below is only making use of rdr, outS, and errStream streams
        //
        // The old-style single response stream Spartan.InvokeCommand() would have been
        // better suited to this example, but then that would mean adding yet another
        // child sub-command that takes only a single PrintStream response output argument.
        //
        // NOTE: The Spartan Flow react interfaces/classes make it much easier to utilize
        // all the streams of an invoked child process sub-command. Take a look at the
        // spartan-react-ex and spartan-cfg-ex example programs for how to use Spartan Flow.
        //
        try (final Reader rdr = new InputStreamReader(rsp.inStream);
             final InputStream childErrS = rsp.errStream;
             final OutputStream childInS = rsp.childInputStream;
             final PrintStream outS = taskOutS;
             final PrintStream errS = taskErrS)
        {
          // this is an example implementation and all that we do here
          // is write the child process output to the original invoker
          // response stream, i.e., merely echo the child output
          final ReadLine lineRdr = new ReadLine(rdr, 1024);
          CharSequence line;
          while ((line = lineRdr.readLine()) != null) {
            if (line.length() <= 0) {
              outS.println();
            } else {
              outS.println(line);
            }
          }
        } catch (Throwable e) {
          final String err = format("exception thrown so exiting thread: %s%n%s", Thread.currentThread().getName(), e);
          log(LL_ERR, err::toString); // logs the error message to the service's stderr
          taskErrS.printf("%nERROR: %s: %s%n", cmd, err); // write's to the invoker's stderr
        }
      };

      workerExecutor.submit(detachedTask); // invoked worker child process will be managed by a supervisor detached task

      errStream = null; // can safely assume that this is solely owned by the detached task now, so null it out

    } catch (Throwable e) { // catch all exceptions here and deal with them (don't let them propagate)
      // if we got here then we never got into task execution so go
      // ahead and log to error output stream back to the invoker,
      // (otherwise is assumed the task now owns and will close it)
      errStream.printf("%nERROR: %s: exception thrown:%n", (args.length > 0 ? args[0] : "{invalid command}"));
      e.printStackTrace(errStream);
    } finally {
      closeStream(outStream, "output stream");
      closeStream(errStream, "error output stream");
      closeStream(inStream, "input stream");
    }
  }

  /**
   * Example Spartan child worker entry-point method.
   * (Does a simulated processing activity.)
   * <p>
   * The annotation declares it is invoked via the command GENETL.
   * When invoked from a shell command line, the sub-command name
   * is case-insensitive.
   * <p>
   * The annotation also supplies Java JVM heap size options.
   * <p>
   * <b>NOTE:</b> a child worker command entry-point method must
   * be declared static.
   *
   * @param args command line arguments passed to the child worker
   *             (first argument is the name of the command invoked)
   * @param outStream the invoked operation can write processing results
   *                  (and/or health check status) to the invoker
   * @param errStream stream for writing error information to the invoker
   * @param inStream stream from receiving input from invoker
   */
  @ChildWorkerCommand(cmd="GENETL", jvmArgs={"-Xms48m", "-Xmx128m"})
  public static void doGenesisEtlProcessing(String[] args, PrintStream outStream, PrintStream errStream,
                                            InputStream inStream)
  {
    print_method_call_info(outStream, clsName, "doGenesisEtlProcessing", args);
    commandForwarder(args, outStream, errStream, inStream, App::doCoreGenesisEtlProcessing);
  }

  private static int doCoreGenesisEtlProcessing(String cmd, String[] args, PrintStream outStream, PrintStream errStream,
                                                InputStream inStream)
  {
    /*### do the real work of the worker child process sub-command here ###*/
    print_method_call_info(outStream, "spartan.test", "invokeGenerateDummyTestOutput", args);
    spartan.test.invokeGenerateDummyTestOutput(cmd, args, outStream, errStream);

    return 0; // worker child process should return a meaningful status code indicating success/failure
  }

  /**
   * Example Spartan child worker entry-point method.
   * (Does a simulated processing activity.)
   * <p>
   * This example illustrates using Spartan technique to
   * allow only one child process to execute this command
   * at any give time (i.e., <b>singleton execution semantics</b>).
   * <p>
   * The annotation declares it is invoked via the command CDCETL.
   * When invoked from a shell command line, the sub-command name
   * is case-insensitive.
   * <p>
   * The annotation also supplies Java JVM heap size options.
   * <p>
   * <b>NOTE:</b> a child worker command entry-point method must
   * be declared static.
   *
   * @param args command line arguments passed to the child worker
   *             (first argument is the name of the command invoked)
   * @param outStream the invoked operation can write processing results
   *                  (and/or health check status) to the invoker
   * @param errStream stream for writing error information to the invoker
   * @param inStream stream from receiving input from invoker
   */
  @ChildWorkerCommand(cmd="CDCETL", jvmArgs={"-Xms48m", "-Xmx128m"})
  public static void doCdcEtlProcessing(String[] args, PrintStream outStream, PrintStream errStream,
                                        InputStream inStream)
  {
    print_method_call_info(outStream, clsName, "doCdcEtlProcessing", args);
    commandForwarder(args, outStream, errStream, inStream, App::doCoreCdcEtlProcessing);
  }

  private static int doCoreCdcEtlProcessing(String cmd, String[] args, PrintStream outStream, PrintStream errStream,
                                            InputStream inStream)
  {
    int status_code = 0; // zero indicates successful completion

    final String pidFileBaseName = String.join("-", "spartan-ex", cmd).toLowerCase();

    if (Spartan.isFirstInstance(pidFileBaseName)) { // a guard mechanism used by singleton worker sub-commands

      /*### do the real work of the worker child process sub-command here ###*/
      print_method_call_info(outStream, "spartan.test", "invokeGenerateDummyTestOutput", args);
      spartan.test.invokeGenerateDummyTestOutput(cmd, args, outStream, errStream);

    } else {
      final String errmsg = format("Worker command %s is already running", cmd);
      errStream.printf("%nWARNING: %s%n", errmsg);
      log(LL_WARN, errmsg::toString); // logs to the service's stderr
      status_code = 1;
    }

    return status_code; // worker child process should return a meaningful status code indicating success/failure
  }
}