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
import java.lang.reflect.Method;
import java.net.MalformedURLException;
import java.net.URISyntaxException;
import java.nio.file.FileSystems;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
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

  @SupervisorCommand("RUNGENESIS")
  public void runGenesis(String[] args, PrintStream rspStream) {
    final String methodName = "runGenesis";
    try (final PrintStream rsp = rspStream) {
      assert(args.length > 0);
      print_method_call_info(rsp, methodName, args);
    }
  }

  @SupervisorCommand("INVOKECHILDCMD")
  public void invokeChildCmd(String[] args, PrintStream rspStream) {
    final String methodName = "invokeChildCmd";
    try {
      assert(args.length > 0);
      print_method_call_info(rspStream, methodName, args);
      if (args.length < 2) {
        rspStream.println("ERROR: no child command specified - insufficient commandline arguments");
        return;
      }
      if (args[0].equalsIgnoreCase(args[1])) {
        rspStream.printf("ERROR: cannot invoke self, %s, as child command to run%n", args[1]);
        return;
      }
      final InvokeResponse rtn = Spartan.invokeCommand(Arrays.copyOfRange(args, 1, args.length));
      _pids.add(rtn.childPID);
      final InputStream childInputStrm = rtn.inStream;
      final PrintStream rspOutput = rspStream; // the task will take ownership of the response stream
      rspStream = null;
      // Asynchronously consume output from the invoked command and write it to the response stream
      workerThread.execute(() -> {
        try (final LineNumberReader lineRdr = new LineNumberReader(new InputStreamReader(childInputStrm), 1024 * 2)) {
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
          log(LL_ERR, () ->
              format("%s exception so exiting thread:%n%s%n", Thread.currentThread().getName(), memBufWrtr.toString())
            );
        }
      });
    } catch (Exception e) {
      uncheckedExceptionThrow(e);
    } finally {
      if (rspStream != null) {
        rspStream.close();
      }
    }
  }

  @ChildWorkerCommand(cmd="GENESIS", jvmArgs={"-server", "-Xms128m", "-Xmx512m"})
  public static void doGenesisEtl(String[] args, PrintStream rspStream) {
    final String methodName = "doGenesisEtl";
    try (final PrintStream rsp = rspStream) {
      assert(args.length > 0);
      print_method_call_info(rsp, methodName, args);
      final Class<?> testSpartanCls = Class.forName("TestSpartan");
      final Method childWorkerDoCommand = testSpartanCls.getMethod("childWorkerDoCommand", String[].class,
          PrintStream.class);
      childWorkerDoCommand.invoke(null, args, rsp);
    } catch (Exception e) {
      uncheckedExceptionThrow(e);
    }
  }

  @ChildWorkerCommand(cmd="CDC")
  public static void doCdcEtl(String[] args, PrintStream rspStream) {
    final String methodName = "doCdcEtl";
    try (final PrintStream rsp = rspStream) {
      assert(args.length > 0);
      print_method_call_info(rsp, methodName, args);
    }
  }
}