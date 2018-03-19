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

import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.PrintStream;
import java.io.Reader;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.Arrays;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicInteger;

import spartan.Spartan;
import spartan.SpartanBase;
import spartan.annotations.ChildWorkerCommand;
import spartan.annotations.SupervisorCommand;
import spartan.annotations.SupervisorMain;

public class App extends SpartanBase {
  private static final String clsName = App.class.getName();
  private static final AtomicInteger workerThreadNbr = new AtomicInteger(1);
  private static final ExecutorService workerThread = Executors.newCachedThreadPool(r -> {
    final Thread t = new Thread(r);
    t.setDaemon(true);
    t.setName(String.format("%s-pool-thread-#%d", clsName, workerThreadNbr.getAndIncrement()));
    return t;
  });
  private static final Set<Integer> _pids = ConcurrentHashMap.newKeySet(53);

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

    // TODO: do one time service initialization here

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

  /**
   * Diagnostic helper method that prints debug info for a called command method.
   * @param rspStream output stream to print info to
   * @param methodName name of the command method that was called
   * @param args arguments that were passed to the invoked method
   */
  private static void print_method_call_info(PrintStream rspStream, String methodName, String[] args) {
    final String output = String.join("\" \"", args);
    rspStream.printf("invoked %s.%s(\"%s\")%n", clsName, methodName, output);
  }

  /**
   * An example <i>supervisor</i> command entry-point method. This method will
   * be invoked on a thread running in the <i>supervisor</i> Java JVM process.
   *
   * <p/>The implementation generates the Fibonacci Sequence. An upper ceiling can
   * be passed as an argument.
   *
   * @param args command line arguments passed to the <i>supervisor</i> process
   *             (first argument is the name of the command invoked)
   * @param rspStream the generated results are written to this response stream
   */
  @SupervisorCommand("GENFIB")
  public void generateFibonacciSequence(String[] args, PrintStream rspStream) {
    final String methodName = "generateFibonacciSequence";
    try (final PrintStream rsp = rspStream) {
      assert(args.length > 0);
      print_method_call_info(rsp, methodName, args);

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
      e.printStackTrace(rspStream);
      uncheckedExceptionThrow(e);
    }
  }

  /**
   * An example <i>supervisor</i> command entry-point method that serves as
   * a utility mechanism to get the <i>supervisor</i> process itself to invoke
   * a specified child worker command, with ownership of its result output.
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
   * @param rspStream output from the child worker will be echoed to this response
   *                  stream
   */
  @SupervisorCommand("INVOKECHILDCMD")
  public void invokeChildCmd(String[] args, PrintStream rspStream) {
    final String methodName = "invokeChildCmd";
    try {
      assert(args.length > 0);
      print_method_call_info(rspStream, methodName, args);

      if (args.length < 2) {
        rspStream.println("ERROR: no child command specified - insufficient command line arguments");
        return;
      }
      if (args[0].equalsIgnoreCase(args[1])) {
        rspStream.printf("ERROR: cannot invoke self, %s, as child command to run%n", args[1]);
        return;
      }

      final InvokeResponse rtn = Spartan.invokeCommand(Arrays.copyOfRange(args, 1, args.length));
      _pids.add(rtn.childPID);

      final InputStream childInputStrm = rtn.inStream;
      final PrintStream rspOutput = rspStream; // the async task will take ownership of the response stream
      rspStream = null; // so null it out so that the finally block no longer will close it

      // Asynchronously consume output from the invoked command and write it to the response stream
      workerThread.execute(() -> {
        try (final Reader rdr = new InputStreamReader(childInputStrm)) {
          try (final PrintStream rsp = rspOutput) {
            // this is an example implementation and all that we do here
            // is write the child process output to the original invoker
            // response stream, i.e., merely echo the child output
            final ReadLine lineRdr = new ReadLine(rdr, 1024);
            CharSequence line;
            while ((line = lineRdr.readLine()) != null) {
              if (line.length() <= 0) {
                rsp.println();
              } else {
                rsp.println(line);
              }
            }
          }
        } catch (Throwable e) {
          String err = format("%s exception so exiting thread:%n%s%n", Thread.currentThread().getName(), e.toString());
          log(LL_ERR, err::toString);
        }
      });
    } catch (Throwable e) {
      e.printStackTrace(rspStream);
      uncheckedExceptionThrow(e);
    } finally {
      if (rspStream != null) {
        rspStream.close();
      }
    }
  }

  /**
   * Example Spartan child worker entry-point method.
   * (Does a simulated processing activity.)
   *
   * <p/>The annotation declares it is invoked via the command
   * GENETL.
   *
   * <p/>The annotation also supplies Java JVM heap size options.
   *
   * @param args command line arguments passed to the child worker
   *        (first argument is the name of the command invoked)
   * @param rspStream the invoked operation can write results
   *        (and/or health check status) to the invoker
   */
  @ChildWorkerCommand(cmd="GENETL", jvmArgs={"-Xms128m", "-Xmx324m"})
  public static void doGenesisEtlProcessing(String[] args, PrintStream rspStream) {
    final String methodName = "doGenesisEtlProcessing";
    try (final PrintStream rsp = rspStream) {
      assert(args.length > 0);
      print_method_call_info(rsp, methodName, args);

      doSimulatedEtlProcessing(args, rsp);

    } catch (Throwable e) {
      e.printStackTrace(rspStream);
      uncheckedExceptionThrow(e);
    }
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
   * CDCETL.
   *
   * <p/>The annotation also supplies Java JVM heap size options.
   *
   * @param args command line arguments passed to the child worker
   *        (first argument is the name of the command invoked)
   * @param rspStream the invoked operation can write results
   *        (and/or health check status) to the invoker
   */
  @ChildWorkerCommand(cmd="CDCETL", jvmArgs={"-Xms128m", "-Xmx324m"})
  public static void doCdcEtlProcessing(String[] args, PrintStream rspStream) {
    final String methodName = "doCdcEtlProcessing";
    try (final PrintStream rsp = rspStream) {
      assert(args.length > 0);
      print_method_call_info(rsp, methodName, args);

      final String cmd_lc = args[0].toLowerCase();
      final String pidFileBaseName = String.join("-", "spartan-ex", cmd_lc);

      if (Spartan.isFirstInstance(pidFileBaseName)) {

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
