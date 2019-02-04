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

import static spartan.Spartan.*;

public class LaunchProgram {
  static {
    System.loadLibrary("spartan-shared");
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