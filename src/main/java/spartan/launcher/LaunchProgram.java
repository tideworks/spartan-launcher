/* LaunchProgram.java

Copyright 2015 - 2018 Tideworks Technology
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
package spartan.launcher;

import java.io.File;
import java.io.IOException;
import java.nio.file.FileSystems;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.function.BiFunction;

import static spartan.Spartan.*;

public class LaunchProgram {
  static {
    final String spartanSharedLibName = "spartan-shared";
    final BiFunction<String, String[], Path> getPath = FileSystems.getDefault()::getPath;
    final String[] mappedLibName = { System.mapLibraryName(spartanSharedLibName) };
    final String libsPath = System.getProperty("java.library.path", ".");
    final String[] libPaths = libsPath.split(File.pathSeparator);
    boolean found = false;
    for (final String libDirpath : libPaths) {
      final Path libFilepath = getPath.apply(libDirpath, mappedLibName);
      if (Files.exists(libFilepath)) {
        BasicFileAttributes attrs;
        try {
          attrs = Files.readAttributes(libFilepath, BasicFileAttributes.class);
        } catch (IOException e) {
          System.err.printf("WRN: failed accessing native lib:%n\t\"%s\"%n", libFilepath);
          e.printStackTrace(System.err);
          System.err.flush();
          continue;
        }
        if (!attrs.isDirectory() && !attrs.isOther()) {
          try {
            System.err.printf("DBG: attempt loading native lib:%n\t\"%s\"%n", libFilepath);
            System.load(libFilepath.toString());
            found = true;
            System.err.printf("DBG: completed loading native lib:%n\t\"%s\"%n", libFilepath);
          } catch (Throwable e) {
            System.err.printf("ERR: failed loading native lib:%n\t\"%s\"%n", libFilepath);
            e.printStackTrace(System.err);
            System.err.flush();
          }

        }
      }
    }
    if (!found) {
      System.loadLibrary(spartanSharedLibName);
    }
  }
  public static native void log(int level, String msg);
  public static native InvokeResponse invokeCommand(String[] args)
      throws ClassNotFoundException, InvokeCommandException, InterruptedException;
  public static native InvokeResponseEx invokeCommandEx(String[] args)
      throws ClassNotFoundException, InvokeCommandException, InterruptedException;
  public static native void killSIGINT(int pid) throws KillProcessException;
  public static native void killSIGTERM(int pid) throws KillProcessException;
  public static native void killSIGKILL(int pid) throws KillProcessException;
  public static native void killProcessGroupSIGINT(int pid) throws KillProcessException, KillProcessGroupException;
  public static native void killProcessGroupSIGTERM(int pid) throws KillProcessException, KillProcessGroupException;
  public static native void killProcessGroupSIGKILL(int pid) throws KillProcessException, KillProcessGroupException;
  public static native long getSysThreadID();
  public static native void sysThreadInterrupt(long sysThrdID);
  public static native boolean isFirstInstance(String progName);
}