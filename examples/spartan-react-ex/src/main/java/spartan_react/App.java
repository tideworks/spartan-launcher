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
package spartan_react;

import spartan.Spartan;
import spartan.SpartanBase;
import spartan.annotations.ChildWorkerCommand;
import spartan.annotations.SupervisorCommand;
import spartan.annotations.SupervisorMain;
import spartan.fstreams.Flow.*;

import java.io.*;
import java.nio.file.*;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.function.BiFunction;
import java.util.function.Consumer;
import java.util.function.Predicate;
import java.util.zip.GZIPInputStream;

//import static java.nio.file.Files.walkFileTree;
import static spartan.util.io.Files.walkFileTree;
import static java.nio.file.StandardOpenOption.CREATE;
import static java.nio.file.StandardOpenOption.TRUNCATE_EXISTING;
import static java.util.stream.Collectors.toList;

@SuppressWarnings({"unused", "JavaDoc"})
public class App extends SpartanBase {
  private static final String clsName = App.class.getSimpleName();
  private static final Set<Integer> _pids = ConcurrentHashMap.newKeySet(53);
  private static final int BUF_SIZE = 0x4000;

  /**
   * Designated Spartan service entry-point method. This will
   * be the <i>supervisor</i> Java JVM process.
   * <p>
   * Java JVM heap options for the <i>supervisor</i> process
   * should be specified in the example application's Spartan
   * config.ini file.
   *
   * @param args options to be processed for the service
   *             initialization (the option -service will
   *             be one of them as is required by Spartan)
   */
  @SupervisorMain
  public static void main(String[] args) {
    log(LL_INFO, "hello world - supervisor service has started!"::toString);

    // TODO: do any one time service initialization here

    enterSupervisorMode(_pids);

    log(LL_INFO, "exiting normally"::toString);
  }

  /**
   * This SpartanBase method is being overridden because this example
   * <i>supervisor</i> will track pids of child workers that it invokes itself
   * in its own <i>_pids</i> collection. So when a child worker process
   * completes, its pid needs to be removed from said <i>_pids</i> collection.
   * (The <i>supervisor</i> will add the child worker's pid to the collection
   * when it is invoked.)
   * <p>
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

  @SupervisorCommand("MASS_UNCOMPRESS")
  public void massUncompress(String[] args, PrintStream outStream, PrintStream errStream, InputStream inStream) {
    print_method_call_info(errStream, clsName, "massUncompress", args);
    commandForwarder(args, outStream, errStream, inStream, this::coreMassUncompress);
  }

  private void coreMassUncompress(String cmd, String[] args, PrintStream outStream, PrintStream errStream,
                                  InputStream inStream) throws Exception
  {
    if (args.length < 1) {
      errStream.printf("ERROR: %s: expected directory path - insufficient commandline arguments%n", cmd);
      return;
    }

    final Path dirPath = validatePath(cmd, args[0], errStream, true);
    if (dirPath == null) {
      return;
    }

    final List<Path> gzFilesList = new ArrayList<>(40);

    // obtain list of .gz files to be decompressed
    final EnumSet<FileVisitOption> options = EnumSet.of(FileVisitOption.FOLLOW_LINKS);
    walkFileTree(dirPath, options/*EnumSet.noneOf(FileVisitOption.class)*/, Integer.MAX_VALUE,
          new SimpleFileVisitor<Path>() {
            @Override
            public FileVisitResult preVisitDirectory(Path dir, BasicFileAttributes attrs) throws IOException {
              outStream.print(dir);
              outStream.println(File.separatorChar);
              return FileVisitResult.CONTINUE;
            }
            @Override
            public FileVisitResult visitFile(Path file, BasicFileAttributes attrs) throws IOException {
              if (file.getFileName().toString().endsWith(".gz")) {
                gzFilesList.add(dirPath.getFileSystem().getPath(file.toString()));
                outStream.println(file);
              }
              return FileVisitResult.CONTINUE;
            }
          });

    final int processedCount = gzFilesList.isEmpty()
          ? 0 : processGzFiles(cmd, dirPath, gzFilesList, outStream, errStream);

    outStream.printf("INFO: %s: done! %d of %d files processed%n", cmd, processedCount, gzFilesList.size());
  }

  private static Path validatePath(String cmd, String pathStr, PrintStream errS, boolean asDirectory) {
    final String expectation = asDirectory ? "directory" : "file";
    final Path path = FileSystems.getDefault().getPath(pathStr);
    if (!Files.exists(path)) {
      errS.printf("ERROR: %s: specified %s path does not exist: \"%s\"%n", cmd, expectation, path);
      return null;
    }
    final Predicate<Path> affirmPath = asDirectory ? Files::isDirectory : Files::isRegularFile;
    if (!affirmPath.test(path)) {
      errS.printf("ERROR: %s: specified path is not a %s: \"%s\"%n", cmd, expectation, path);
      return null;
    }
    return path;
  }

  /**
   * Core method that does the work of setting up Spartan Flow subsciptions to
   * handle the processing of all the streams of invoked worker child processes
   * (one worker spawned per each input file - two output streams per worker).
   * <p>
   * <b>NOTE:</b> An Executor can be selected (instead of accepting the default)
   * to customize thread pool behavior, i.e., could use a constrained thread
   * pool executor that is initialized in size based on number of CPU processors):
   * {@link spartan.fstreams.Flow#subscribe(ExecutorService, InvokeResponseEx)}
   * accepts an exectutor as the first argument.
   * <p>
   * After all subscriptions are established then uses the subscriber that method
   * {@link Subscriber#start()} was invoked on to obtain a {@link FuturesCompletion};
   * it then calls {@link FuturesCompletion#take()} to harvest the future result
   * of each task.
   *
   * @param cmd name of supervisor sub-command that has been invoked (for logging purposes)
   * @param srcDir the parent directory of the list of input files
   * @param gzFilesList list of .gz files to be uncompressed
   * @param outS the output stream for writing progress information
   * @param errS the output stream for writing errors
   * @return the number child processes with completion results was successfully obtained
   * @throws IOException
   * @throws InvokeCommandException
   * @throws InterruptedException
   * @throws ClassNotFoundException
   */
  private static int processGzFiles(String cmd, Path srcDir, List<Path> gzFilesList, PrintStream outS, PrintStream errS)
        throws IOException, InvokeCommandException, InterruptedException, ClassNotFoundException
  {
    final BiFunction<String, String[], Path> getPath = srcDir.getFileSystem()::getPath;
    final List<Path> errLogFiles = new ArrayList<>(gzFilesList.size());
    final Set<Integer> childrenPIDs = new LinkedHashSet<>(); // use for audit check

    Subscriber subscriber = null;

    for(final Path inputFile : gzFilesList) {
      final String inputFileName = inputFile.getFileName().toString();
      final int lastIndex = inputFileName.lastIndexOf(".gz");
      final String outputFileName = inputFileName.substring(0, lastIndex);
      final Path errOutFile = getPath.apply(inputFile.getParent().toString(), new String[]{outputFileName + ".err"});
      final Path outputFile = getPath.apply(inputFile.getParent().toString(), new String[]{outputFileName});
      final OutputStream errOutFileStream = Files.newOutputStream(errOutFile, CREATE, TRUNCATE_EXISTING);
      final OutputStream outputFileStream = Files.newOutputStream(outputFile, CREATE, TRUNCATE_EXISTING);

      errLogFiles.add(errOutFile);

      // invoking worker child process to do the desired transformation action on an input file...
      final InvokeResponseEx rsp = Spartan.invokeCommandEx("UN_GZIP", inputFile.toString());
      _pids.add(rsp.childPID);
      childrenPIDs.add(rsp.childPID);

      subscriber = subscriber == null ? spartan.fstreams.Flow.subscribe(rsp) : subscriber.subscribe(rsp);
      subscriber
            .onError((errStrm, subscription) -> copyWithClose(errStrm, errOutFileStream, subscription))
            .onNext((outStrm,  subscription) -> copyWithClose(outStrm, outputFileStream, subscription));
    }

    if (subscriber == null) return 0;

    outS.printf("INFO: %s: subscriptions added for %d files, now start processing...%n", cmd, childrenPIDs.size());

    final FuturesCompletion pendingFutures = subscriber.start();

    final ExecutorService executor = pendingFutures.getExecutor();

    final Set<String> pidStrs = new LinkedHashSet<>(); // for collecting returned child process pids as string values

    int i = 0, count = pendingFutures.count(); // number of task the ExecutorCompletionService will be overseeing
    try {
      while(count-- > 0) {
        try {
          final Integer childPID = pendingFutures.take().get();
          _pids.remove(childPID);
          childrenPIDs.remove(childPID);
          pidStrs.add(childPID.toString());
          i++;
        } catch (ExecutionException e) {
          final Throwable cause = e.getCause() != null ? e.getCause() : e;
          errS.printf("%nERROR: %s: exception encountered in sub task:%n", cmd);
          cause.printStackTrace(errS);
          errS.println();
        } catch (InterruptedException e) {
          errS.printf("%nWARN: %s: interruption occurred - processing may not be completed!%n", cmd);
        }
      }
    } finally {
      executor.shutdown();
      waitOnExecServiceTermination(executor, 5);
    }

    if (!childrenPIDs.isEmpty()) {
      final String[] remaining = childrenPIDs.stream().map(Object::toString).collect(toList()).toArray(new String[0]);
      errS.printf("ERROR: %s: %d subscription(s) futures results unharvested:%n%s%n",
            cmd, remaining.length, String.join(",", remaining));
    }

    final Predicate<Path> isEmptyFile = path -> {
      boolean rtn = true;
      try {
        rtn = Files.size(path) <= 0;
      } catch (IOException e) {
        uncheckedExceptionThrow(e);
      }
      return rtn;
    };

    final Consumer<Path> deleteFile = path -> {
      try {
        Files.deleteIfExists(path);
      } catch (IOException e) {
        uncheckedExceptionThrow(e);
      }
    };

    // remove all empty error log files
    errLogFiles.stream().filter(isEmptyFile).forEach(deleteFile);

    outS.printf("INFO: %s: %d subscription tasks completed by %d process pid(s):%n%s%n",
          cmd, i, pidStrs.size(), String.join(",", pidStrs.toArray(new String[0])));

    return pidStrs.size(); // returning number of harvested subscriptions (should equal number of input files)
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

  private static long copyWithClose(InputStream fromStream, OutputStream toStream, Subscription subscription) {
    long count = 0;
    try (final InputStream from = fromStream;
         final OutputStream to = toStream;
         final OutputStream ignored = subscription.getRequestStream())
    {
      count = copy(from, to);
    } catch (IOException e) {
      uncheckedExceptionThrow(e);
    }
    return count;
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

  @ChildWorkerCommand(cmd = "UN_GZIP", jvmArgs = {"-server", "-Xms64m", "-Xmx128m"})
  public static void uncompressGzFile(String[] args, PrintStream outStream, PrintStream errStream,
                                      InputStream inStream)
  {
//    print_method_call_info(errStream, clsName, "uncompressGzFile", args);
    commandForwarder(args, outStream, errStream, inStream, App::uncompressGzFileImpl);
  }

  private static int uncompressGzFileImpl(String cmd, String[] args, PrintStream outStream, PrintStream errStream,
                                          InputStream inStream)
  {
    if (args.length < 1) {
      errStream.printf("ERROR: %s: expected .gz file path - insufficient commandline arguments%n", cmd);
      return 1;
    }

    final Path filePath = validatePath(cmd, args[0], errStream, false);

    // worker child process should return a meaningful status code indicating success/failure (zero == success)
    return filePath != null ? uncompressGzFileCore(cmd, filePath, outStream, errStream, inStream) : 1;
  }

  private static int uncompressGzFileCore(String cmd, Path filePath, OutputStream outS, PrintStream errS,
                                          InputStream inS)
  {
    try {
      final InputStream inputStream = Files.newInputStream(filePath);
      byte[] buf = new byte[BUF_SIZE];
      try (final GZIPInputStream gzipInputStream = new GZIPInputStream(inputStream, 4 * BUF_SIZE)) {
        for(;;) {
          final int n = gzipInputStream.read(buf);
          if (n == -1) break;
          outS.write(buf, 0, n);
        }
        outS.flush();
      }
    } catch (IOException e) {
      errS.printf("ERROR: %s: failed un-compressing file: \"%s\"%n", cmd, filePath);
      e.printStackTrace(errS);
      return 1; // non-zero indicates that failed
    }
    return 0; // zero indicates successful completion
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
  @SuppressWarnings({"unchecked", "unused", "UnusedReturnValue"})
  private static <T extends Throwable, R> R uncheckedExceptionThrow(Throwable t) throws T { throw (T) t; }
}
