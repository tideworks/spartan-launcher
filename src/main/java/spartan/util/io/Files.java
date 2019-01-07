package spartan.util.io;

import java.io.File;
import java.io.IOException;
import java.net.URI;
import java.nio.file.*;
import java.nio.file.attribute.BasicFileAttributes;
import java.nio.file.attribute.FileTime;
import java.util.EnumSet;
import java.util.Iterator;
import java.util.Set;

@SuppressWarnings({"unused", "WeakerAccess"})
public final class Files {
  private static final short DT_DIR = 4;
  private static final short DT_REG = 8;
  private static final short DT_LNK = 10;
  private static final boolean isPosix;
  private static final WalkFileTreeFunc walkFileTreeCB;
  static {
    isPosix = FileSystems.getDefault().supportedFileAttributeViews().contains("posix");
    if (isPosix) {
      System.loadLibrary("walk-file-tree");
      walkFileTreeCB = Files::walkFileTreeInvokeNative;
    } else {
      walkFileTreeCB = java.nio.file.Files::walkFileTree;
    }
  }
  private Files() {}

  private static native void fsyncDirectory(String dir) throws IOException;

  @FunctionalInterface
  private interface WalkFileTreeFunc {
    Path walk(Path start, Set<FileVisitOption> options, int maxDepth, FileVisitor<? super Path> visitor)
          throws IOException;
  }

  private interface WalkFileTreeVisitor {
    int preVisitDirectory(Path dirPath, BasicFileAttributes attrs) throws IOException;
    int visitFile(Path filePath, BasicFileAttributes attrs) throws IOException;
    int visitFileFailed(Path filePath, IOException exc) throws IOException;
    int postVisitDirectory(Path dirPath, IOException exc) throws IOException;
  }

  private native void walk_file_tree(char separator_char, int maxdepth, boolean follow_links, Path startDir,
                                     WalkFileTreeVisitor visitor) throws IOException;

  private static Path walkFileTreeInvokeNative(Path start, Set<FileVisitOption> options, int maxDepth,
                                               FileVisitor<? super Path> visitor) throws IOException
  {
    new Files().walk_file_tree(File.separatorChar, maxDepth, options.contains(FileVisitOption.FOLLOW_LINKS), start,
          new WalkFileTreeVisitor() {
            @Override
            public int preVisitDirectory(Path dirPath, BasicFileAttributes attrs) throws IOException {
              return visitor.preVisitDirectory(dirPath, attrs).ordinal();
            }

            @Override
            public int visitFile(Path filePath, BasicFileAttributes attrs) throws IOException {
              return visitor.visitFile(filePath, attrs).ordinal();
            }

            @Override
            public int visitFileFailed(Path filePath, IOException exc) throws IOException {
              return visitor.visitFileFailed(filePath, exc).ordinal();
            }

            @Override
            public int postVisitDirectory(Path dirPath, IOException exc) throws IOException {
              return visitor.postVisitDirectory(dirPath, exc).ordinal();
            }
          });
    return start;
  }

  @SuppressWarnings({"unchecked", "unused", "UnusedReturnValue"})
  private static <T extends Throwable, R> R uncheckedExceptionThrow(Throwable t) throws T { throw (T) t; }

  private static BasicFileAttributes makeBasicFileAttributes(Path start) {
    return new BasicFileAttributes() {
      private String filePath = null;
      private boolean isRegularFile = false;
      private boolean isDirectory = false;
      private boolean isSymbolicLink = false;
      private boolean isOther = false;
      private BasicFileAttributes selfAttrs = null;
      private BasicFileAttributes getSelfAttrs() {
        if (selfAttrs == null) {
          assert filePath != null;
          try {
            final Path filePathObj = start.getFileSystem().getPath(filePath);
            selfAttrs = java.nio.file.Files.readAttributes(filePathObj, BasicFileAttributes.class);
          } catch (IOException e) {
            uncheckedExceptionThrow(e);
          }
        }
        return selfAttrs;
      }
      private void init(String filepath, short d_type) {
        this.filePath = filepath;
        this.isRegularFile = false;
        this.isDirectory = false;
        this.isSymbolicLink = false;
        this.isOther = false;
        this.selfAttrs = null;
        switch (d_type) {
          case DT_DIR:
            this.isDirectory = true;
            break;
          case DT_REG:
            this.isRegularFile = true;
            break;
          case DT_LNK:
            this.isSymbolicLink = true;
            break;
          default:
            this.isOther = true;
        }
      }
      @Override
      public FileTime lastModifiedTime() {
        return getSelfAttrs().lastModifiedTime();
      }
      @Override
      public FileTime lastAccessTime() {
        return getSelfAttrs().lastAccessTime();
      }
      @Override
      public FileTime creationTime() {
        return getSelfAttrs().creationTime();
      }
      @Override
      public boolean isRegularFile() {
        return isRegularFile;
      }
      @Override
      public boolean isDirectory() {
        return isDirectory;
      }
      @Override
      public boolean isSymbolicLink() {
        return isSymbolicLink;
      }
      @Override
      public boolean isOther() {
        return isOther;
      }
      @Override
      public long size() {
        return getSelfAttrs().size();
      }
      @Override
      public Object fileKey() {
        return getSelfAttrs().fileKey();
      }
    };
  }
  private static Path makePath(Path start) {
    return new Path() {
      private String filePath = null;
      private Path fileName = null;
      private Path self = null;
      private Path getSelfPath() {
        if (self == null) {
          assert filePath != null;
          self = start.getFileSystem().getPath(filePath);
        }
        return self;
      }
      private void init(String filePath, Path fileName) {
        this.filePath = filePath;
        this.fileName = fileName;
        this.self = null;
      }
      @Override
      public FileSystem getFileSystem() {
        return start.getFileSystem();
      }
      @Override
      public boolean isAbsolute() {
        return fileName != null && getSelfPath().isAbsolute();
      }
      @Override
      public Path getRoot() {
        return fileName == null ? null : getSelfPath().getRoot();
      }
      @Override
      public Path getFileName() {
        return fileName != null ? fileName : getSelfPath();
      }
      @Override
      public Path getParent() {
        return fileName == null ? null : getSelfPath().getParent();
      }
      @Override
      public int getNameCount() {
        return fileName == null ? 1 : getSelfPath().getNameCount();
      }
      @Override
      public Path getName(int index) {
        return getSelfPath().getName(index);
      }
      @Override
      public Path subpath(int beginIndex, int endIndex) {
        return getSelfPath().subpath(beginIndex, endIndex);
      }
      @Override
      public boolean startsWith(Path other) {
        return getSelfPath().startsWith(other);
      }
      @Override
      public boolean startsWith(String other) {
        return filePath.startsWith(other);
      }
      @Override
      public boolean endsWith(Path other) {
        return getSelfPath().endsWith(other);
      }
      @Override
      public boolean endsWith(String other) {
        return filePath.endsWith(other);
      }
      @Override
      public Path normalize() {
        return getSelfPath().normalize();
      }
      @Override
      public Path resolve(Path other) {
        return getSelfPath().resolve(other);
      }
      @Override
      public Path resolve(String other) {
        return getSelfPath().resolve(other);
      }
      @Override
      public Path resolveSibling(Path other) {
        return getSelfPath().resolveSibling(other);
      }
      @Override
      public Path resolveSibling(String other) {
        return getSelfPath().resolveSibling(other);
      }
      @Override
      public Path relativize(Path other) {
        return getSelfPath().relativize(other);
      }
      @Override
      public URI toUri() {
        return getSelfPath().toUri();
      }
      @Override
      public Path toAbsolutePath() {
        return getSelfPath().toAbsolutePath();
      }
      @Override
      public Path toRealPath(LinkOption... options) throws IOException {
        return getSelfPath().toRealPath(options);
      }
      @Override
      public File toFile() {
        return new File(filePath);
      }
      @Override
      public WatchKey register(WatchService watcher, WatchEvent.Kind<?>[] events, WatchEvent.Modifier... modifiers)
              throws IOException
      {
        return getSelfPath().register(watcher, events, modifiers);
      }
      @Override
      public WatchKey register(WatchService watcher, WatchEvent.Kind<?>... events) throws IOException {
        return getSelfPath().register(watcher, events);
      }
      @Override
      public Iterator<Path> iterator() {
        return getSelfPath().iterator();
      }
      @Override
      public int compareTo(Path other) {
        return getSelfPath().compareTo(other);
      }
      @Override
      public String toString() {
        return filePath;
      }
    };
  }

  public static Path walkFileTree(Path start, FileVisitor<? super Path> visitor) throws IOException {
    return walkFileTreeCB.walk(start, EnumSet.noneOf(FileVisitOption.class), Integer.MAX_VALUE, visitor);
  }
  public static Path walkFileTree(Path start, Set<FileVisitOption> options, int maxDepth,
                                  FileVisitor<? super Path> visitor) throws IOException
  {
    return walkFileTreeCB.walk(start, options, maxDepth, visitor);
  }
  public static void fsyncDirectory(Path dir) throws IOException {
    if (isPosix) {
      fsyncDirectory(dir.toString());
    }
  }
}
