package spartan.util.io;

import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.nio.file.*;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.EnumSet;
import java.util.concurrent.atomic.AtomicBoolean;

import static org.junit.jupiter.api.Assertions.*;

class FilesTest {
  private static final boolean isPosix = FileSystems.getDefault().supportedFileAttributeViews().contains("posix");

  @Test
  void walkFileTree() {
  }

  @Test
  void walkFileTree1() throws IOException {
    final Path wft_lib_dirpath = FileSystems.getDefault()
          .getPath("target", "cmake", "build", "Release").toAbsolutePath();
    final String wft_native_lib_name = System.mapLibraryName("walk-file-tree");
    if (isPosix) {
      assertEquals(wft_native_lib_name, "libwalk-file-tree.so");
    } else {
      assertEquals(wft_native_lib_name, "walk-file-tree.dll");
    }
    final Path wft_lib_filepath = Paths.get(wft_lib_dirpath.toString(), wft_native_lib_name);
    if (isPosix) {
      System.load(wft_lib_filepath.toString());
    }
    final String java_home = System.getenv("JAVA_HOME");
    assertNotNull(java_home);
    assertFalse(java_home.isEmpty());
    final Path javaHomePath = Paths.get(java_home);
    final String jvm_native_lib_name = System.mapLibraryName("jvm");
    final Path jvmlib = Paths.get(jvm_native_lib_name).getFileName();
    final AtomicBoolean wasFound = new AtomicBoolean(false);
    Files.walkFileTree(javaHomePath, EnumSet.noneOf(FileVisitOption.class), Integer.MAX_VALUE,
          new SimpleFileVisitor<Path>()
    {
      @Override
      public FileVisitResult visitFile(Path file, BasicFileAttributes attrs) throws IOException {
        if (file.getFileName().compareTo(jvmlib) == 0) {
          wasFound.set(true);
          System.out.printf("test walkFileTree1(): found jvm shared library%n\t%s%n", file);
          return FileVisitResult.TERMINATE;
        }
        return FileVisitResult.CONTINUE;
      }
    });
    assertTrue(wasFound.get());
  }
}
