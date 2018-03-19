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
import static java.nio.file.StandardOpenOption.CREATE;
import static java.nio.file.StandardOpenOption.READ;
import static java.nio.file.StandardOpenOption.TRUNCATE_EXISTING;

import java.io.*;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.nio.file.FileSystems;
import java.nio.file.Files;
import java.nio.file.InvalidPathException;
import java.nio.file.Path;
import java.util.*;
import java.util.function.Consumer;
import java.util.function.Function;

import com.beust.jcommander.JCommander;
import com.beust.jcommander.Parameter;
import com.beust.jcommander.Parameters;

import spartan.Spartan;
import spartan.SpartanBase;
import spartan.annotations.ChildWorkerCommand;
import spartan.annotations.SupervisorMain;

public class App extends SpartanBase {
  private static final String clsName = App.class.getName();
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
  private static Path programDirPath;
  private static CommandLineArgs s_args;

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

    void save(File serializedCfgSettingsFile) throws Exception {
      try (final OutputStream outStream = Files.newOutputStream(serializedCfgSettingsFile.toPath(),
              CREATE, TRUNCATE_EXISTING)) {
        try (final ObjectOutputStream objOutStream = new ObjectOutputStream(outStream)) {
          objOutStream.writeObject(this);
        }
      } catch (IOException e) {
        throw new Exception(saveSerializedCfcSettingsErrMsg, e);
      }
    }

    static CommandLineArgs load(File serializedCfgSettingsFile) throws Exception {
      try (final InputStream inStream = Files.newInputStream(serializedCfgSettingsFile.toPath(), READ)) {
        try (final ObjectInputStream objInStream = new ObjectInputStream(inStream)) {
          return (CommandLineArgs) objInStream.readObject();
        }
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
        args = argsCopy.toArray(new String[argsCopy.size()]);
      }
    }

    // JCommander object is used to parse command line options (and config file derived options)
    // and applies them to then initialize the state of the CommandLineArgs s_args object, which
    // there after is referenced for obtaining any application configuration settings.

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
      // now serialize instance of CommandLineArgs s_args object to a file so can be loaded by child worker processes
      final File serializedCfgSettingsFile = new File(getProgramDirectory(), serializedCfcSettingsFileName);
      try {
        s_args.save(serializedCfgSettingsFile);
        log(LL_INFO, s_args::toString); // dump toString() output to app logging console; TODO should comment this out
      } catch (Exception e) {
        log(LL_ERR, e::toString);
        log(LL_ERR, control_C_Msg::toString);
        return;
      }

      log(LL_INFO, () -> format("%s: hello world - supervisor service has started!%n", programName));

      enterSupervisorMode();

      log(LL_INFO, () -> format("%s exiting normally", programName));
    }
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
   * Will de-serialize instance of CommandLineArgs object from a file for obtaining application configuration settings.
   *
   * <p/>Call this helper method upon entry to a Spartan child worker command before proceeding to execute the command's
   * implementation. Any configuration initialization performed at service startup will now be available to the child
   * process to use.
   *
   * <p/><b>NOTE:</b> Assign the return result into the static member variable <i>s_args</i>
   *
   * @param args arguments that were passed to the invoked command method
   * @return instance of de-serialized CommandLineArgs object
   * @throws Exception if file not found or loading from file fails
   */
  private static CommandLineArgs childWorkerInitialization(String[] args, final Consumer<JCommander> apply) throws Exception {
    setProgramDirectory();
    final File serializedCfgSettingsFile = new File(getProgramDirectory(), serializedCfcSettingsFileName);
    if (serializedCfgSettingsFile.exists()) {
      final CommandLineArgs loadedCfg = CommandLineArgs.load(serializedCfgSettingsFile);
      final JCommander jcmdr = new JCommander(loadedCfg);
      if (args.length > 0) {
        jcmdr.parse(args); // now apply the arguments of the invoked command method
      }
      jcmdr.parse(String.join("=", childWorkerOptn, Boolean.TRUE.toString()));
      if (apply != null) {
        apply.accept(jcmdr);
      }
      return loadedCfg;
    } else {
      throw new Exception(format(serCfgFileNotFoundErrMsgFmt, serializedCfgSettingsFile));
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
   * <p/>
   * The annotation declares it is invoked via the command
   * GENETL.
   * <p/>
   * The annotation also supplies Java JVM heap size options.
   *
   * @param args command line arguments passed to the child worker
   *        (first argument is the name of the command invoked)
   * @param rspStream the invoked operation can write results
   *        (and/or health check status) to the invoker
   */
  @ChildWorkerCommand(cmd="GENETL", jvmArgs={"-Xms96m", "-Xmx184m"})
  public static void doGenesisEtlProcessing(String[] args, PrintStream rspStream) {
    final String methodName = "doGenesisEtlProcessing";
    try (final PrintStream rsp = rspStream) {
      assert(args.length > 0);
      print_method_call_info(rsp, methodName, args);

      s_args = childWorkerInitialization(Arrays.copyOfRange(args, 1, args.length),
              jcmdr -> jcmdr.parse(String.join("=", genesisChildWorkerOptn, Boolean.TRUE.toString())));
      rspStream.println(s_args); // dump toString() output to response stream

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

      s_args = childWorkerInitialization(Arrays.copyOfRange(args, 1, args.length),
              jcmdr -> jcmdr.parse(String.join("=", cdcChildWorkerOptn, Boolean.TRUE.toString())));
      rspStream.println(s_args); // dump toString() output to response stream; TODO should comment this out

      final String cmd_lc = args[0].toLowerCase();
      final String pidFileBaseName = String.join("-", s_args.programName, cmd_lc);

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
   * <p/>
   * <b>NOTE:</b> The presence of <i>-run-forever</i> option will cause it to run perpetually.
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
