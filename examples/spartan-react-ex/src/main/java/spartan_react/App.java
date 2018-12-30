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

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.PrintStream;
import java.nio.file.*;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.EnumSet;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

public class App extends SpartanBase {
  private static final String clsName = App.class.getName();
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

  @SupervisorCommand("MASS_UNCOMPRESS")
  public void massUncompress(String[] args, PrintStream outStream, PrintStream errStream, InputStream inStream) {
    final String methodName = "massUncompress";
    assert args.length > 0;

    try (final PrintStream outS = outStream; final PrintStream errS = errStream; final InputStream inS = inStream) {
      print_method_call_info(errStream, methodName, args);
      if (args.length < 2) {
        errStream.println("ERROR: expected directory path - insufficient commandline arguments");
        return;
      }
      final Path dirPath = validateDirectoryPath(args[1], errS);
      if (dirPath == null) return;

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
                  outS.println(file);
                }
                return FileVisitResult.CONTINUE;
              }
            });

      outS.println("done!");
    } catch (IOException e) {
      e.printStackTrace(errStream);
    }
  }

  private Path validateDirectoryPath(String dirPathStr, PrintStream errS) {
    final Path dirPath = FileSystems.getDefault().getPath(dirPathStr);
    if (!Files.exists(dirPath)) {
      errS.printf("ERROR: specified directory path does not exist: \"%s\"%n", dirPath);
      return null;
    }
    if (!Files.isDirectory(dirPath)) {
      errS.printf("ERROR: specified path is not a directory: \"%s\"%n", dirPath);
      return null;
    }
    return dirPath;
  }
}