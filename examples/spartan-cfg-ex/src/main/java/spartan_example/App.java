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
import java.nio.file.FileSystems;
import java.nio.file.InvalidPathException;
import java.nio.file.Path;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.Consumer;
import java.util.function.Function;

import com.beust.jcommander.JCommander;
import com.beust.jcommander.Parameter;
import com.beust.jcommander.Parameters;

import spartan.Spartan;
import spartan.SpartanBase;
import spartan.annotations.ChildWorkerCommand;
import spartan.annotations.SupervisorCommand;
import spartan.annotations.SupervisorMain;
import spartan.fstreams.Flow.FuturesCompletion;
import spartan.fstreams.Flow.Subscriber;
import spartan.fstreams.Flow.Subscription;
import spartan.util.io.ByteArrayOutputStream;

@SuppressWarnings({"JavaDoc", "unused"})
public class App extends SpartanBase {
  private static final String clsName = App.class.getName();
  private static final String resetBackoffToken = "\nRESET_BACKOFF_INDEX\n";
  private static final String control_C_Msg = "Press Control-C to exit";
  private static final String invalidProgramPathErrMsg = "program directory not set, unable to proceed:";
  private static final String saveSerializedCfcSettingsErrMsg = "failed saving application configuration settings:";
  private static final String loadSerializedCfcSettingsErrMsg = "failed loading application configuration settings:";
  private static final String cfgFileLoadErrMsgFmt = "failure loading configuration file from:%n\t\"%s\"%n%s";
  private static final String cfgFileNotFoundWrnMsgFmt = "configuration file \"%s\" not found - using default settings";
  private static final String serCfgFileNotFoundErrMsgFmt = "application configuration settings file \"%s\" not found";
  private static final String childWorkerOptn = "-child-worker";
  private static final String genesisChildWorkerOptn = "-genesis-child-worker";
  private static final String cdcChildWorkerOptn = "-cdc-child-worker";
  private static final String cfgSettingsFileName = "config.properties";
  private static final String serializedCfcSettingsFileName = "config.ser";
  private static final Set<Integer> _pids = ConcurrentHashMap.newKeySet(53);
  private static final AtomicReference<byte[]> serializedCfgSettings = new AtomicReference<>(null);
  private static final int BUF_SIZE = 0x4000;
  private static Path programDirPath;
  private static CommandLineArgs s_args;
  private final ExecutorService taskExecutor;

  @Parameters(separators="= ")
  private static final class CommandLineArgs implements Serializable {
    private static final long serialVersionUID = 1L;

    String programName;

    CommandLineArgs(String programName) {
      this.programName = programName;
    }

    @Parameter(names={ "-?", "-h", "-help" }, help=true, description="show options")
    boolean help = false;

    @Parameter(names="-v", description="INFO level logging verbosity (optional; defaults to INFO level)")
    boolean isInfoLevel = true;

    @Parameter(names="-v1", description="DEBUG level logging verbosity")
    boolean isDebugLevel = false;

    @Parameter(names="-v2", description="TRACE level logging verbosity")
    boolean isTraceLevel = false;

    @Parameter(names=childWorkerOptn, description="Supervisor process instance running as a service")
    boolean isChildWorker = false;

    @Parameter(names=genesisChildWorkerOptn, description="Genesis ETL child worker process instance")
    boolean isGenesisChildWorker = false;

    @Parameter(names=cdcChildWorkerOptn, description="CDC ETL child worker process instance")
    boolean isCdcChildWorker = false;

    @Parameter(names="-dmpcd", description="enables dumping intermediate state into diagnostic files", arity = 1)
    boolean dumpChangeData = false;

    @Parameter(names="-poll", description="use to specify the CDC polling interval (in seconds)")
    long pollingInterval = 10;
/*
    @Parameter(names="-src", description="source (transactional) database connection string",
            converter=ParseSrcDBConnection.class)
    OptionsResult srcDb = OptionsResult.none;

    @Parameter(names="-dst", description="destination (query) database connection string",
        converter=ParseDstDBConnection.class)
    OptionsResult dstDb = OptionsResult.none;

    @Parameter(names="-src-schema",
            description="file path to a .yml config file which list tables and their columns",
            converter=ParseConfigurationFilePath.class)
    OptionsResult srcSchemaCfgFilePath = OptionsResult.none;
*/
    @Parameter(names="-outputdir", description="list of one or more output folders - separated by ':' or ';'")
    String outputdir = ".";

    @Parameter(names = "-setdir", description = "Folder containing log files")
    String setdir = "";

    @Parameter(names= { "-cnt", "--continueExtraction" },
            description="When extracting from log files, continue where left off (default is true)", arity = 1)
    boolean isContinueExtraction = true;

    @Parameter(hidden=true)
    List<String> unknowns = new ArrayList<>();

    void save(OutputStream outStream) throws Exception {
      try (final ObjectOutputStream objOutStream = new ObjectOutputStream(outStream)) {
        objOutStream.writeObject(this);
        objOutStream.flush();
      } catch (IOException e) {
        throw new Exception(saveSerializedCfcSettingsErrMsg, e);
      }
    }

    static CommandLineArgs load(InputStream inStream) throws Exception {
      try (final ObjectInputStream objInStream = new ObjectInputStream(inStream)) {
        return (CommandLineArgs) objInStream.readObject();
      } catch (IOException|ClassNotFoundException e) {
        throw new Exception(loadSerializedCfcSettingsErrMsg, e);
      }
    }

    /**
     * For debug diagnostic purposes only.
     * @return text representation of object state
     */
    @Override
    public String toString() {
      return "CommandLineArgs{" +
                     "programName='" + programName + '\'' +
                     ", help=" + help +
                     ", isInfoLevel=" + isInfoLevel +
                     ", isDebugLevel=" + isDebugLevel +
                     ", isTraceLevel=" + isTraceLevel +
                     ", isChildWorker=" + isChildWorker +
                     ", isGenesisChildWorker=" + isGenesisChildWorker +
                     ", isCdcChildWorker=" + isCdcChildWorker +
                     ", dumpChangeData=" + dumpChangeData +
                     ", pollingInterval=" + pollingInterval +
                     ", outputdir='" + outputdir + '\'' +
                     ", setdir='" + setdir + '\'' +
                     ", isContinueExtraction=" + isContinueExtraction +
                     ", unknowns=" + unknowns +
                     '}';
    }
  }

  // watchdog service instance initialization (will run as a singleton object managed by Spartan runtime)
  {
    final AtomicInteger threadNumber = new AtomicInteger(1);

    taskExecutor = Executors.newCachedThreadPool(r -> {
      final Thread t = new Thread(r);
      t.setDaemon(true);
      t.setName(format("%s-watchdog-thread-#%d", programName, threadNumber.getAndIncrement()));
      return t;
    });

    // A service shutdown handler (will respond to SIGINT/SIGTERM signals and Spartan stop command);
    // basically in this example program it just manages the ExecutorService thread pools for shutdown
    final Thread shutdownHandler = new Thread(() -> {
      taskExecutor.shutdown();
      waitOnExecServiceTermination(taskExecutor, 5); // will await up to 5 seconds
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
    s_args = new CommandLineArgs(programName); // object state that will hold application configuration settings

    try {
      setProgramDirectory();
    } catch (Exception e) {
      log(LL_ERR, e::toString);
      log(LL_ERR, control_C_Msg::toString);
      return;
    }

    boolean isServiceInstance = false;

    {// Remove '-service' option from args array
      final List<String> argsCopy = new ArrayList<>(Arrays.asList(args));
      if (isServiceInstance = argsCopy.removeIf("-service"::equalsIgnoreCase)) {
        args = argsCopy.toArray(new String[0]);
      }
    }

    // JCommander object is used to parse command line options (and config file derived options)
    // and applies them to then initialize the state of the CommandLineArgs s_args object, which
    // thereafter is referenced for obtaining any application configuration settings.

    // load configuration file (if exist) and apply settings
    JCommander jcmdr = loadConfigFile(new File(cfgSettingsFileName), App::processCommandLineArgs);
    if (jcmdr != null) {
      jcmdr.parse(args); // any redundant command line options will now take precedence over config file settings
    } else {
      // no config file settings were specified so processing only command line options (if any)
      jcmdr = processCommandLineArgs(args);
    }

    if (s_args.help) {
      jcmdr.usage();
      JCommander.getConsole().println(control_C_Msg);
    } else if (isServiceInstance) {
      try {
        // now serialize instance of CommandLineArgs s_args object to
        // a memory buffer so can be loaded by child worker processes
        final ByteArrayOutputStream outputSerCfg = new ByteArrayOutputStream(BUF_SIZE);
        s_args.save(outputSerCfg);
        serializedCfgSettings.set(outputSerCfg.toByteArray());
        log(LL_INFO, s_args::toString); // dump toString() output to app logging console; TODO should comment this out
      } catch (Exception e) {
        log(LL_ERR, e::toString);
        log(LL_ERR, control_C_Msg::toString);
        return;
      }

      log(LL_INFO, "hello world - supervisor service has started!"::toString);

      enterSupervisorMode();

      log(LL_INFO, "exiting normally"::toString);
    }
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
   * Set the program directory - first try user HOME directory
   * if its defined or else the current working directory. Use
   * this directory for files that require write access.
   * @throws Exception if the directory is invalid for some reason
   */
  private static void setProgramDirectory() throws Exception {
    assert(programDirPath == null);
    try {
      String progDirPath = System.getenv("HOME"); // user home directory
      if (progDirPath == null || progDirPath.isEmpty()) {
        progDirPath = "."; // current working directory
      }
      programDirPath = FileSystems.getDefault().getPath(progDirPath);
    } catch (InvalidPathException e) {
      throw new Exception(invalidProgramPathErrMsg, e);
    }
  }

  private static File getProgramDirectory() {
    assert(programDirPath != null);
    return programDirPath.toFile();
  }

  private static JCommander processCommandLineArgs(String[] args) {
    assert(s_args != null);
    final JCommander jcmdr = new JCommander(s_args);
    jcmdr.setProgramName(programName);
    jcmdr.setCaseSensitiveOptions(false);
    jcmdr.setAcceptUnknownOptions(true);
    jcmdr.parse(args);
    return jcmdr;
  }

  /**
   * Helper method that loads config file options; options from the config file may be
   * superseded by a command line use of a same option.
   *
   * @param cfgFile                File object referencing config file to load and process
   * @param processCommandLineArgs callback that applies config file options to the
   *                               application options state.
   * @return the merged JCommander instance
   */
  private static JCommander loadConfigFile(File cfgFile, final Function<String[], JCommander> processCommandLineArgs) {
    if (cfgFile.exists() || (cfgFile = new File(getProgramDirectory(), cfgFile.getName())).exists()) {
      try {
        try (final FileInputStream in = new FileInputStream(cfgFile)) {
          final Properties props = new Properties();
          props.load(in);
          if (props.size() > 0) {
            final Set<Map.Entry<Object, Object>> entries = props.entrySet();
            final List<String> cfgAsArgs = new ArrayList<>(entries.size());
            for (final Map.Entry<Object, Object> entry : entries) {
              final String key = (String) entry.getKey();
              final String val = (String) entry.getValue();
              if (processVerbosityCfgFileOption(key, val)) continue;
              if (val != null && !val.isEmpty()) {
                cfgAsArgs.add(format("-%s=%s", key, val));
              } else {
                cfgAsArgs.add(format("-%s", key));
              }
            }
            return processCommandLineArgs.apply(cfgAsArgs.toArray(new String[cfgAsArgs.size()]));
          }
        }
      } catch (IOException e) {
        final String errmsg = format(cfgFileLoadErrMsgFmt, cfgFile, e);
        log(LL_ERR, errmsg::toString);
      }
    } else {
      final String warnmsg = format(cfgFileNotFoundWrnMsgFmt, cfgFile);
      log(LL_WARN, warnmsg::toString);
    }
    return null;
  }

  /**
   * Processes the verbosity property as found in a configuration file. Can be used to set
   * the logging level verbosity via a config file setting.
   *
   * @param option should always be the string "verbosity"
   * @param value the logging level to be set
   * @return true if set logging verbosity level, otherwise false
   */
  private static boolean processVerbosityCfgFileOption(final String option, final String value) {
    switch (option.toLowerCase()) {
      case "verbosity":
        // set logging verbosity level
        if (value != null && !value.isEmpty()) {
          switch (value.toLowerCase()) {
            case "trace":
              s_args.isTraceLevel = true;
              s_args.isInfoLevel = false;
              break;
            case "debug":
              s_args.isDebugLevel = true;
              s_args.isInfoLevel = false;
              break;
            case "info":
              s_args.isInfoLevel = true;
              break;
            case "warn":
              s_args.isInfoLevel = false;
              break;
            case "error":
              s_args.isInfoLevel = false;
              break;
            default:
              return false;
          }
        }
        break;
      default:
        return false;
    }
    return true;
  }

  /**
   * Will de-serialize instance of CommandLineArgs object from a file for obtaining
   * application configuration settings.
   * <p>
   * Call this helper method upon entry to a Spartan child worker command before
   * proceeding to execute the command's implementation. Any configuration initialization
   * performed at service startup will now be available to the child process to use.
   * <p>
   * <b>NOTE:</b> Assign the return result into the static member variable <i>s_args</i>
   *
   * @param inS stream for reading configuration state
   * @param args arguments that were passed to the invoked command method
   * @return instance of de-serialized CommandLineArgs object
   * @throws Exception if file not found or loading from file fails
   */
  private static CommandLineArgs childWorkerInitialization(final InputStream inS, final String[] args,
                                                           final Consumer<JCommander> apply) throws Exception
  {
    final CommandLineArgs loadedCfg = CommandLineArgs.load(inS);
    final JCommander jcmdr = new JCommander(loadedCfg);
    if (args.length > 0) {
      jcmdr.parse(args); // now apply the arguments of the invoked command method
    }
    jcmdr.parse(String.join("=", childWorkerOptn, Boolean.TRUE.toString()));
    if (apply != null) {
      apply.accept(jcmdr);
    }
    return loadedCfg;
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
  public static void doGenesisEtlProcessing(String[] args, PrintStream outStream, PrintStream errStream, InputStream inStream) {
    final String methodName = "doGenesisEtlProcessing";
    assert args.length > 0;

    final String cmd = args[0];

    int status_code = 0;

    try (final PrintStream outS = outStream; final PrintStream errS = errStream; final InputStream inS = inStream) {
      print_method_call_info(outS, methodName, args);

      s_args = childWorkerInitialization(inS, Arrays.copyOfRange(args, 1, args.length),
              jcmdr -> jcmdr.parse(String.join("=", genesisChildWorkerOptn, Boolean.TRUE.toString())));
      errS.println(s_args); // dump toString() output to error stream

      spartan.test.invokeGenerateDummyTestOutput(args, outS, errS);

    } catch (Throwable e) { // catch all exceptions here and deal with them (don't let them propagate)
      errStream.printf("%nERROR: %s: exception thrown:%n", cmd);
      e.printStackTrace(errStream);
      status_code = 1;
    }

    System.exit(status_code);
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
  public static void doCdcEtlProcessing(String[] args, PrintStream outStream, PrintStream errStream, InputStream inStream) {
    final String methodName = "doCdcEtlProcessing";
    assert args.length > 0;

    final String cmd = args[0];

    int status_code = 0;

    try (final PrintStream outS = outStream; final PrintStream errS = errStream; final InputStream inS = inStream) {
      print_method_call_info(errS, methodName, args);

      s_args = childWorkerInitialization(inS, Arrays.copyOfRange(args, 1, args.length),
            jcmdr -> jcmdr.parse(String.join("=", cdcChildWorkerOptn, Boolean.TRUE.toString())));
      errS.println(s_args); // dump toString() output to error stream

      final String pidFileBaseName = String.join("-", s_args.programName, cmd).toLowerCase();

      if (Spartan.isFirstInstance(pidFileBaseName)) {

        final ScheduledExecutorService scheduler = Executors.newScheduledThreadPool(1);
        scheduler.scheduleAtFixedRate(() -> errS.print(resetBackoffToken), 3, 3, TimeUnit.SECONDS);

        spartan.test.invokeGenerateDummyTestOutput(args, outS, errS);

      } else {
        final String errmsg = format("Child command %s is already running", cmd);
        errS.printf("WARNING: %s%n", errmsg);
        log(LL_WARN, errmsg::toString);
        status_code = 1;
      }
    } catch (Throwable e) { // catch all exceptions here and deal with them (don't let them propagate)
      errStream.printf("%nERROR: %s: exception thrown:%n", cmd);
      e.printStackTrace(errStream);
      status_code = 1;
    }

    System.exit(status_code);
  }

  @SupervisorCommand("INVOKECHILDCMD")
  public void invokeChildCmd(String[] args, PrintStream outStream, PrintStream errStream, InputStream inStream) {
    final String methodName = "invokeChildCmd";
    try {
      assert args.length > 0;
      print_method_call_info(outStream, methodName, args);
      final String cmd = args[0];

      if (args.length < 2) {
        errStream.println("ERROR: no child command specified - insufficient command line arguments");
        return;
      }
      if (cmd.equalsIgnoreCase(args[1])) {
        errStream.printf("ERROR: cannot invoke self, %s, as child command to run%n", args[1]);
        return;
      }

      final String childSubCmd = args[1];

      final Runnable detachedTask = () -> {
        try (final PrintStream outS = outStream; final PrintStream errS = errStream; final InputStream inS = inStream) {
          final InvokeResponseEx rsp = Spartan.invokeCommandEx(Arrays.copyOfRange(args, 1, args.length));
          _pids.add(rsp.childPID);

          // send the service's serialized configuration to the spawned child process
          final ByteArrayInputStream byteArrayInputStream = new ByteArrayInputStream(serializedCfgSettings.get());
          copy(byteArrayInputStream, rsp.childInputStream);

          // now use a Spartan Flow subscription to process all input streams of the spawned child process
          final Subscriber subscriber = spartan.fstreams.Flow.subscribe(taskExecutor, rsp);
          final FuturesCompletion futuresCompletion = subscriber
              .onError((errStrm, subscription) -> copyWithClose(errStrm, errS, subscription))
              .onNext((outStrm,  subscription) -> copyWithClose(outStrm, outS, subscription))
              .start();

          int count = futuresCompletion.count();
          while(count-- > 0) {
            try {
              final Integer childPID = futuresCompletion.take().get();
              _pids.remove(childPID);
            } catch (ExecutionException e) {
              final Throwable cause = e.getCause() != null ? e.getCause() : e;
              errS.printf("%nERROR: %s: exception encountered in sub task:%n", childSubCmd);
              cause.printStackTrace(errS);
              errS.println();
            } catch (InterruptedException e) {
              errS.printf("%nWARN: %s: interruption occurred - processing may not be completed!%n", childSubCmd);
            }
          }
        } catch (InterruptedException e) {
          errStream.printf("%nWARN: %s: interruption occurred - processing may not be completed!%n", childSubCmd);
        } catch (Throwable e) {
          errStream.printf("%nERROR: %s: exception thrown:%n", childSubCmd);
          e.printStackTrace(errStream);
        }
      };

      taskExecutor.submit(detachedTask);

    } catch (Throwable e) {
      errStream.printf("%nERROR: %s: exception thrown:%n", (args.length > 0 ? args[0] : "{invalid command}"));
      e.printStackTrace(errStream);
    }
  }

  private static long copyWithClose(InputStream fromStream, OutputStream toStream, Subscription subscription) {
    try (final InputStream from = fromStream;
         final OutputStream to = toStream;
         final OutputStream ignored = subscription.getRequestStream())
    {
      return copy(from, to);
    } catch (IOException e) {
      uncheckedExceptionThrow(e);
    }
    return 0; // will never reach this statement, but hushes compiler
  }

  private static long copy(InputStream from, OutputStream to) throws IOException {
    assert from != null;
    assert to != null;
    byte[] buf = new byte[BUF_SIZE];
    long total = 0;
    for (;;) {
      int n = from.read(buf);
      if (n == -1) break;
      to.write(buf, 0, n);
      total += n;
    }
    to.flush();
    return total;
  }
}