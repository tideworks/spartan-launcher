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
import java.util.ArrayList;
import java.util.EnumSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutionException;
import java.util.function.BiFunction;
import java.util.function.Predicate;
import java.util.zip.GZIPInputStream;

import static java.nio.file.StandardOpenOption.CREATE;
import static java.nio.file.StandardOpenOption.TRUNCATE_EXISTING;

public class App extends SpartanBase {
  private static final Set<Integer> _pids = ConcurrentHashMap.newKeySet(53);

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

  private static void print_method_call_info(PrintStream rspStream, String methodName, String[] args) {
    final String output = String.join("\" \"", args);
    rspStream.printf("DEBUG: invoked %s.%s(\"%s\")%n", App.class.getName(), methodName, output);
  }

  @SuppressWarnings("unused")
  @SupervisorCommand("MASS_UNCOMPRESS")
  public void massUncompress(String[] args, PrintStream outStream, PrintStream errStream, InputStream inStream) {
    final String methodName = "massUncompress";
    assert args.length > 0;

    final String cmd = args[0];

    try (final PrintStream outS = outStream; final PrintStream errS = errStream; final InputStream ignore = inStream) {
      print_method_call_info(errS, methodName, args);
      if (args.length < 2) {
        errS.printf("ERROR: %s: expected directory path - insufficient commandline arguments%n", cmd);
        return;
      }
      final Path dirPath = validatePath(cmd, args[1], errS, true);
      if (dirPath == null) return;

      final List<Path> gzFilesList = new ArrayList<>(40);

      // obtain list of .gz files to be decompressed
      final EnumSet<FileVisitOption> options = EnumSet.of(FileVisitOption.FOLLOW_LINKS);
      Files.walkFileTree(dirPath, options/*EnumSet.noneOf(FileVisitOption.class)*/, Integer.MAX_VALUE,
            new SimpleFileVisitor<Path>() {
              @Override
              public FileVisitResult preVisitDirectory(Path dir, BasicFileAttributes attrs) throws IOException {
                outS.print(dir);
                outS.println(File.separatorChar);
                return FileVisitResult.CONTINUE;
              }
              @Override
              public FileVisitResult visitFile(Path file, BasicFileAttributes attrs) throws IOException {
                if (file.getFileName().toString().endsWith(".gz")) {
                  gzFilesList.add(file);
                  outS.println(file);
                }
                return FileVisitResult.CONTINUE;
              }
            });

      int processedCount = 0;

      if (!gzFilesList.isEmpty()) {
        processedCount = processGzFiles(cmd, dirPath, gzFilesList, outS, errS);
      }

      outS.printf("done! %d of %d files processed%n", processedCount, gzFilesList.size());
    } catch (IOException | InterruptedException | ClassNotFoundException | InvokeCommandException e) {
      e.printStackTrace(errStream);
    }
  }

  private static Path validatePath(String cmd, String pathStr, PrintStream errS, boolean asDirectory) {
    final Path path = FileSystems.getDefault().getPath(pathStr);
    if (!Files.exists(path)) {
      errS.printf("ERROR: %s: specified directory path does not exist: \"%s\"%n", cmd, path);
      return null;
    }
    final String expectation = asDirectory ? "directory" : "file";
    final Predicate<Path> affirmPath = asDirectory ? Files::isDirectory : Files::isRegularFile;
    if (!affirmPath.test(path)) {
      errS.printf("ERROR: %s: specified path is not a %s: \"%s\"%n", cmd, expectation, path);
      return null;
    }
    return path;
  }

  private static int processGzFiles(String cmd, Path srcDir, List<Path> gzFilesList, PrintStream outS, PrintStream errS)
        throws IOException, InvokeCommandException, InterruptedException, ClassNotFoundException
  {
    final BiFunction<String, String[], Path> getPath = srcDir.getFileSystem()::getPath;

    Subscriber subscriber = null;

    for(final Path inputFile : gzFilesList) {
      final String inputFileName = inputFile.getFileName().toString();
      final int lastIndex = inputFileName.lastIndexOf(".gz");
      final String outputFileName = inputFileName.substring(0, lastIndex);
      final Path errOutFile = getPath.apply(inputFile.getParent().toString(), new String[]{outputFileName + ".err"});
      final Path outputFile = getPath.apply(inputFile.getParent().toString(), new String[]{outputFileName});
      final OutputStream errOutFileStream = Files.newOutputStream(errOutFile, CREATE, TRUNCATE_EXISTING);
      final OutputStream outputFileStream = Files.newOutputStream(outputFile, CREATE, TRUNCATE_EXISTING);

      final InvokeResponseEx rsp = Spartan.invokeCommandEx("UN_GZIP", inputFile.toString());

      _pids.add(rsp.childPID);

      subscriber = subscriber == null ? spartan.fstreams.Flow.subscribe(rsp) : subscriber.subscribe(rsp);
      subscriber
            .onError((errStrm, subscription) -> copyOutToFile(errStrm, errOutFileStream, subscription))
            .onNext((outStrm,  subscription) -> copyOutToFile(outStrm, outputFileStream, subscription));
    }

    assert subscriber != null;
    final FuturesCompletion futures = subscriber.start();

    int count = futures.count();
    final List<String> pids = new ArrayList<>(count);
    int i = 0;

    while(count-- > 0) {
      try {
        final Integer childPID = futures.take().get();
        _pids.remove(childPID);
        pids.add(childPID.toString());
      } catch (ExecutionException e) {
        final Throwable cause = e.getCause() != null ? e.getCause() : e;
        errS.printf("ERROR: %s: exception encountered in sub task:%n", cmd);
        e.printStackTrace(errS);
      } catch (InterruptedException e) {
        errS.printf("WARN: %s: interruption occurred%n", cmd);
      }
    }

    outS.printf("INFO: %s: completed child processes:%n%s%n", cmd, String.join(", ", pids.toArray(new String[0])));

    return 0;
  }

  private static void copyOutToFile(InputStream errStrm, OutputStream errOutFileStream, Subscription subscription) {
    byte[] buf = new byte[4 * 1024];
    try (final InputStream errS = errStrm; final OutputStream errOutFS = errOutFileStream) {
      for(;;) {
        final int n = errS.read(buf);
        if (n <= 0) break;
        errOutFS.write(buf, 0, n);
      }
    } catch (IOException e) {
      subscription.cancel();
      uncheckedExceptionThrow(e);
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

  @SuppressWarnings("unused")
  @ChildWorkerCommand(cmd = "UN_GZIP", jvmArgs = {"-server", "-Xms64m", "-Xmx128m"})
  public static void uncompressGzFile(String[] args, PrintStream outStream, PrintStream errStream, InputStream inStream) {
    final String methodName = "uncompressGzFile";
    assert args.length > 0;

    final String cmd = args[0];

    try (final PrintStream outS = outStream; final PrintStream errS = errStream; final InputStream inS = inStream) {
      print_method_call_info(errS, methodName, args);
      if (args.length < 2) {
        errS.printf("ERROR: %s: expected .gz file path - insufficient commandline arguments%n", cmd);
        return;
      }

      final Path filePath = validatePath(cmd, args[1], errS, false);
      if (filePath == null) return;

      uncompressGzFileHelper(cmd, filePath, outS, errS, inS);
    } catch (IOException e) {
      e.printStackTrace(errStream);
    }
  }

  private static void uncompressGzFileHelper(String cmd, Path filePath, OutputStream outS, PrintStream errS,
                                             InputStream inS)
  {
    try {
      final InputStream inputStream = Files.newInputStream(filePath);
      byte[] buf = new byte[4 + 1024];
      try (final GZIPInputStream gzipInputStream = new GZIPInputStream(inputStream, 14 * 1024)) {
        for(;;) {
          final int n = gzipInputStream.read(buf);
          if (n <= 0) break;
          outS.write(buf, 0, n);
        }
        outS.flush();
      }
    } catch (IOException e) {
      errS.printf("ERROR: %s: failed un-compressing file: \"%s\"%n", cmd, filePath);
      e.printStackTrace(errS);
    }
  }
}