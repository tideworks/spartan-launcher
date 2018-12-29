/* test.java

Copyright 2017 Tideworks Technology
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
import static java.nio.file.StandardOpenOption.CREATE;
import static java.nio.file.StandardOpenOption.READ;
import static java.nio.file.StandardOpenOption.TRUNCATE_EXISTING;

import java.io.CharArrayWriter;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.LineNumberReader;
import java.io.ObjectInputStream;
import java.io.PrintStream;
import java.io.PrintWriter;
import java.net.MalformedURLException;
import java.net.URISyntaxException;
import java.nio.file.FileSystems;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.LinkedHashSet;
import java.util.Properties;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicInteger;

import spartan.annotations.ChildWorkerCommand;
import spartan.annotations.SupervisorCommand;
import spartan.annotations.SupervisorMain;

@SuppressWarnings("unused")
public final class test extends SpartanBase {
  private static final String clsName = test.class.getName();
  private static final String methodEntryExitFmt = "%s %s.%s(): @@@@@ %s test and print diagnostics output @@@@@%n";
  private static final String runForeverOptn = "-run-forever";
  private static final AtomicInteger workerThreadNbr = new AtomicInteger(1);
  private static final ExecutorService workerThread = Executors.newCachedThreadPool(r -> {
    final Thread t = new Thread(r);
    t.setDaemon(true);
    t.setName(String.format("%s-pool-thread-#%d", clsName, workerThreadNbr.getAndIncrement()));
    return t;
  });
  @SuppressWarnings("WeakerAccess")
  protected static final Set<Integer> _pids = ConcurrentHashMap.newKeySet(53);

  @SuppressWarnings({"unchecked", "UnusedReturnValue"})
  private static <T extends Exception, R> R uncheckedExceptionThrow(Exception t) throws T { throw (T) t; }

  @SuppressWarnings("DuplicateThrows")
  @SupervisorMain
  public static void main(String[] args)
      throws MalformedURLException, IOException, URISyntaxException, ClassNotFoundException
  {
    final String methodName = "main";
    final boolean dbgLogging = args.length > 0 && args[0].equalsIgnoreCase("debug");
    System.out.printf(methodEntryExitFmt, ">>", clsName, methodName, "starting");
    System.out.printf("JVM CLASSPATH: %s%n", System.getProperty("java.class.path"));
    System.out.printf("ENV CLASSPATH: %s%n", System.getenv("CLASSPATH"));
    if (dbgLogging) {
      spartan.CommandDispatchInfo.setDebugLoggingLevel();
    }

    final String currWDir = Paths.get(".").toAbsolutePath().normalize().toString();
    final Path cmdDspInfo_path = FileSystems.getDefault().getPath(currWDir, "cmdDspInfo.ser");

    // serialize system properties and CommandDispatchInfo object to a byte array and then write to a file
    final byte[] byteSerializedData = spartan.CommandDispatchInfo.obtainSerializedSysPropertiesAndAnnotationInfo();
    Files.write(cmdDspInfo_path, byteSerializedData, CREATE, TRUNCATE_EXISTING);

    // deserialize system properties and CommandDispatchInfo object from a file
    spartan.CommandDispatchInfo reconstitutedInfo;
    try (final InputStream inStrm = Files.newInputStream(cmdDspInfo_path, READ)) {
      try (final ObjectInputStream in = new ObjectInputStream(inStrm)) {
        final Properties sysProps = (Properties) in.readObject();
        sysProps.forEach((k, v) -> System.out.printf("%s=%s%n", k, v));
        System.setProperties(sysProps);
        reconstitutedInfo = (CommandDispatchInfo) in.readObject();
      }
    }

    System.out.println(reconstitutedInfo);
    System.out.printf(methodEntryExitFmt, "<<", clsName, methodName, "finished");
    enterSupervisorMode(_pids);
  }

  @Override
  public void childProcessCompletionNotify(int pid) {
    super.childProcessCompletionNotify(pid);
    _pids.remove(pid);
  }

  private static void print_method_call_info(PrintStream rspStream, String methodName, String[] args) {
    final String output = String.join("\" \"", args);
    rspStream.printf("invoked %s.%s(\"%s\")%n", test.class.getName(), methodName, output);
  }

  @SupervisorCommand("GENFIB")
  public void generateFibonacciSequence(String[] args, PrintStream rspStream) {
    final String methodName = "generateFibonacciSequence";
    assert (args.length > 0);

    try (final PrintStream rsp = rspStream) {
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
    }
  }

  @SupervisorCommand("INVOKECHILDCMD")
  public void invokeChildCmd(String[] args, PrintStream outStream, PrintStream errStream, InputStream inStream) {
    final String methodName = "invokeChildCmd";
    assert args.length > 0;

    try (final InputStream inStrm = inStream)  {
      print_method_call_info(errStream, methodName, args);
      if (args.length < 2) {
        errStream.println("ERROR: no child command specified - insufficient commandline arguments");
        return;
      }
      if (args[0].equalsIgnoreCase(args[1])) {
        errStream.printf("ERROR: cannot invoke self, %s, as child command to run%n", args[1]);
        return;
      }
      final InvokeResponse rtn = Spartan.invokeCommand(Arrays.copyOfRange(args, 1, args.length));
      _pids.add(rtn.childPID);
      final InputStream childInStrm = rtn.inStream;
      // the async task will take ownership of these writeable streams
      final PrintStream rspOutput = outStream;
      outStream = null;
      final PrintStream errStrm = errStream;
      errStream = null;
      // Asynchronously consume output from the invoked command and write it to the response stream
      workerThread.execute(() -> {
        try (final LineNumberReader lineRdr = new LineNumberReader(new InputStreamReader(childInStrm), 1024 * 2)) {
          try (final PrintStream rsp = rspOutput) {
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
          final CharArrayWriter memBufWrtr = new CharArrayWriter(1024);
          e.printStackTrace(new PrintWriter(memBufWrtr));
          final String stackTrace = memBufWrtr.toString();
          errStrm.println(stackTrace);
          log(LL_ERR, () ->
                format("%s exception so exiting thread:%n%s%n", Thread.currentThread().getName(), stackTrace)
          );
        }
      });
    } catch (Throwable e) {
      final PrintStream errOut = errStream != null ? errStream : System.err;
      e.printStackTrace(errOut);
    } finally {
      if (outStream != null) {
        try {
          outStream.close();
        } catch (Exception ignore) {}
      }
      if (errStream != null) {
        try {
          errStream.close();
        } catch (Exception ignore) {}
      }
    }
  }

  private static void generateDummyTestOutput(final String inputFilePath, PrintStream rspStream, boolean runForever) {
    final String msg = format("%s.generateDummyTestOutput(%s)", clsName, inputFilePath);
    log(LL_DEBUG, ()->msg);

    // test code - writes some text data to response stream and closes it
    rspStream.printf("DEBUG: %s%n", msg);
    rspStream.flush();
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
      rspStream.printf("DEBUG: test message #%d - duration %d ms%n", i, n);
      if (++i < 0) {
        i = 0; // set back to a non negative integer (wrapping happens if running in forever mode)
      }
    }
  }

  private static void invokeGenerateDummyTestOutput(String[] args, PrintStream rspStream, PrintStream errStream) {
    assert args.length > 0;
    final String cmd = args[0];
    if (args.length > 1) {
      final Set<String> argsSet = new LinkedHashSet<>(Arrays.asList(args));
      final boolean runForever = argsSet.remove(runForeverOptn);
      argsSet.remove(cmd);
      final String[] remain_args = argsSet.toArray(new String[0]);
      final String input_string = format("\"%s\"", String.join("\" \"", remain_args));
      generateDummyTestOutput(input_string, rspStream, runForever);
    } else {
      final String errmsg = format("%s child worker has no input JSON filepath specified to process", cmd.toLowerCase());
      errStream.printf("ERROR: %s%n", errmsg);
      log(LL_ERR, ()->errmsg);
    }
  }

  @ChildWorkerCommand(cmd = "GENESIS", jvmArgs = {"-server", "-Xms96m", "-Xmx256m"})
  public static void doGenesisEtl(String[] args, PrintStream rspStream) {
    final String methodName = "doGenesisEtl";
    assert (args.length > 0);

    int exit_code = 0;
    try (final PrintStream outStrm = rspStream) {
      print_method_call_info(outStrm, methodName, args);
      invokeGenerateDummyTestOutput(args, outStrm, outStrm);
    } catch (Throwable e) {
      e.printStackTrace(rspStream);
      exit_code = 1;
    }
    System.exit(exit_code);
  }

  @ChildWorkerCommand(cmd = "CDC", jvmArgs = {"-server", "-Xms96m", "-Xmx256m"})
  public static void doCdcEtl(String[] args, PrintStream outStream, PrintStream errStream, InputStream inStream) {
    final String methodName = "doCdcEtl";
    assert (args.length > 0);

    final String cmd_lc = args[0].toLowerCase();
    final String pidFileBaseName = String.join("-", test.class.getSimpleName().toLowerCase(), cmd_lc);

    int exit_code = 0;
    if (Spartan.isFirstInstance(pidFileBaseName)) {
      try (final PrintStream outStrm = outStream; final PrintStream errStrm = errStream;
           final InputStream inStrm = inStream)
      {
        print_method_call_info(errStrm, methodName, args); // write diagnostic info to stderr
        invokeGenerateDummyTestOutput(args, outStrm, errStrm);
      } catch (Throwable e) {
        e.printStackTrace(System.err);
        exit_code = 1;
      }
    }
    System.exit(exit_code);
  }
}