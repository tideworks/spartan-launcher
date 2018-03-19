/* Spartan.java

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
package spartan;

public interface Spartan {
  default String getProgramName() { return ""; }
  default void supervisorShutdown() {}
  default void status(java.io.PrintStream statusRspStream) { statusRspStream.close(); }
  default void childProcessNotify(int pid, String commandLine) {}
  default void childProcessCompletionNotify(int pid) {}
  default void supervisorDoCommand(String[] args, java.io.PrintStream rspStream) { rspStream.close(); }

  static final class ThreadContext {
    final Thread currThrd;
    private final long sysThrdID;
    public ThreadContext() {
      this.currThrd = Thread.currentThread();
      this.sysThrdID = LaunchProgram.getSysThreadID();
    }
    public static ThreadContext getCurrentThreadContext() {
      return new ThreadContext();
    }
    public void interrupt() {
      LaunchProgram.sysThreadInterrupt(sysThrdID);
      currThrd.interrupt();
    }
    public void join() throws InterruptedException {
      currThrd.join();
    }
    public void join(long timeout) throws InterruptedException {
      currThrd.join(timeout);
    }
  }

  static final class InvokeResponse {
    public final int childPID;
    public final java.io.InputStream inStream;
    public InvokeResponse(int childPID, java.io.InputStream inStream) {
      this.childPID = childPID;
      this.inStream = inStream;
    }
  }

  @SuppressWarnings("serial")
  static final class InvokeCommandException extends Exception {
    public InvokeCommandException(String message) { super(message); }
    public InvokeCommandException(String message, Throwable cause) { super(message, cause); }
  }

  @SuppressWarnings("serial")
  static final class KillProcessException extends Exception {
    public KillProcessException(String message) { super(message); }
    public KillProcessException(String message, Throwable cause) { super(message, cause); }
  }

  @SuppressWarnings("serial")
  static final class KillProcessGroupException extends Exception {
    public KillProcessGroupException(String message) { super(message); }
    public KillProcessGroupException(String message, Throwable cause) { super(message, cause); }
  }
  // log level verbosity
  static final int LL_FATAL = 6;
  static final int LL_ERR   = 5;
  static final int LL_WARN  = 4;
  static final int LL_INFO  = 3;
  static final int LL_DEBUG = 2;
  static final int LL_TRACE = 1;

  static void log(int level, String msg) {
    LaunchProgram.log(level, msg);
  }
  static InvokeResponse invokeProgramCommand(String progName, String... args)
      throws ClassNotFoundException, InvokeCommandException, InterruptedException {
    return LaunchProgram.invokeProgramCommand(progName, args);
  }
  static InvokeResponse invokeCommand(String... args)
      throws ClassNotFoundException, InvokeCommandException, InterruptedException {
    return LaunchProgram.invokeCommand(args);
  }
  static void killSIGINT(int pid) throws KillProcessException {
    LaunchProgram.killSIGINT(pid);
  }
  static void killSIGKILL(int pid) throws KillProcessException {
    LaunchProgram.killSIGKILL(pid);
  }
  static void killProcessGroupSIGINT(int pid) throws KillProcessException, KillProcessGroupException {
    LaunchProgram.killProcessGroupSIGINT(pid);
  }
  static void killProcessGroupSIGKILL(int pid) throws KillProcessException, KillProcessGroupException {
    LaunchProgram.killProcessGroupSIGKILL(pid);
  }
  static boolean isFirstInstance(String progName) {
    return LaunchProgram.isFirstInstance(progName);
  }
}
