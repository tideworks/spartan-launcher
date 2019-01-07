import java.io.File;
import java.io.IOException;
import java.nio.file.*;
import java.nio.file.attribute.BasicFileAttributes;
import java.time.Duration;
import java.util.EnumSet;
import java.util.concurrent.atomic.AtomicBoolean;

public class Main {
  public static void main(String[] args) throws IOException {
    if (args.length <= 0) {
      final String java_home = System.getenv("JAVA_HOME");
      assert java_home != null && !java_home.isEmpty();
      final Path javaHomePath = Paths.get(java_home);
      final String jvm_native_lib_name = System.mapLibraryName("jvm");
      final Path jvmlib = Paths.get(jvm_native_lib_name).getFileName();
      final AtomicBoolean wasFound = new AtomicBoolean(false);
      final long start = System.currentTimeMillis();
      spartan.util.io.Files.walkFileTree(javaHomePath, EnumSet.noneOf(FileVisitOption.class), Integer.MAX_VALUE,
            new SimpleFileVisitor<Path>() {
              @Override
              public FileVisitResult visitFile(Path file, BasicFileAttributes attrs) throws IOException {
                if (file.getFileName().compareTo(jvmlib) == 0) {
                  wasFound.set(true);
                  System.out.printf("main(): found jvm shared library%n\t%s%n", file);
                  return FileVisitResult.TERMINATE;
                }
                return FileVisitResult.CONTINUE;
              }
            });
      assert wasFound.get();
      final long diff_millisecs = System.currentTimeMillis() - start;
      final Duration totalDuration = Duration.ofMillis(diff_millisecs);
      System.err.printf("%nINFO: Files.walkFileTree(\"%s\") [%s total]%n", javaHomePath, totalDuration);
    } else {
      final String srcDir = args[0];
      final Path srcDirPath = FileSystems.getDefault().getPath(srcDir);
      if (!Files.exists(srcDirPath)) {
        System.err.printf("ERROR: specified directory path doesn't exist:%n\t\"%s\"%n", srcDirPath);
      } else if (!Files.isDirectory(srcDirPath)) {
        System.err.printf("ERROR: specified path is not a directory:%n\t\"%s\"%n", srcDirPath);
      } else {
        final long start = System.currentTimeMillis();
        spartan.util.io.Files.walkFileTree(srcDirPath, EnumSet.noneOf(FileVisitOption.class), Integer.MAX_VALUE,
              new SimpleFileVisitor<Path>(){
                @Override
                public FileVisitResult preVisitDirectory(Path dir, BasicFileAttributes attrs) throws IOException {
                  final String dirStr = dir.toString();
                  System.out.print(dirStr);
                  if (!dirStr.isEmpty()) {
                    final char lastCh = dirStr.charAt(dirStr.length() - 1);
                    if (lastCh != '/' && lastCh != '\\') {
                      System.out.println(File.separatorChar);
                      return FileVisitResult.CONTINUE;
                    }
                  }
                  System.out.println();
                  return FileVisitResult.CONTINUE;
                }
                @Override
                public FileVisitResult visitFile(Path file, BasicFileAttributes attrs) throws IOException {
                  System.out.println(file);
                  return FileVisitResult.CONTINUE;
                }
              });
        final long diff_millisecs = System.currentTimeMillis() - start;
        final Duration totalDuration = Duration.ofMillis(diff_millisecs);
        System.err.printf("%nINFO: Files.walkFileTree(\"%s\") [%s total]%n", srcDirPath, totalDuration);
      }
    }
  }
}
