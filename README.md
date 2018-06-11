# spartan "forking" java program launcher

From Wikipedia: [Fork (system call)](https://en.wikipedia.org/wiki/Fork_%28system_call%29)

> *In computing, particularly in the context of the Unix operating system and its workalikes, fork is an operation whereby a process creates a copy of itself. It is usually a system call, implemented in the kernel. Fork is the primary (and historically, only) method of process creation on Unix-like operating systems.*

## Introduction
The java program launcher, `java` on Posix platforms and `java.exe` on Windows, has remained pretty much the same in functionality since the origin of the Java programming language. This default java program launcher provides a uniform experience for starting the execution of java programs on all supported platforms.

But after two decades the facility for launching java programs is due for a face lift.

- Today the predominate use of the Java language is in writing back-end or middle-ware, server-based software, and in more recent times the interest has shifted to centering on implementing microservices. (Java is also popularly used for Android mobile device computing but is not relevant to discussion here.)

- These days Linux (or Posix) operating systems rule in the realm of server-based software - this is principally because cloud computing data centers mostly rely on a Linux-based OS as the host for their VMs. Also, enterprise computing looking to homogenize development across on-premise data centers and cloud-based computing have gravitated toward Linux as a common denominator.

- Containerization coupled with microservices are dominate trends in server software development and its subsequent deployment. Linux is the most prominent OS platform for containerization because the most widely used container implementations are based on Linux kernel features.

- Java *threads* are problematic when writing high availability, self-healing software - whereas the *process* of modern operating systems represents a much more robust unit of program execution that can be killed with impunity and then the program relaunched (Java threads - well, not so much).

- ssh-based command-line shell connections are a pervasive means of connecting to server host. The ability to interact with services software from a command line can be a nice enabler for both developers and support staff - particularly if the service can respond to sub commands initiated from the command line and do so with practically no special effort required by the programmers of the service. Also, having such command line interaction capability with a running service daemon that is not dependent on TCP sockets insures a tighter default security posture.

In view of these factors it is time to take a new look at the matter of launching Java programs. Why not devise a new kind of java program launcher that is better fitted to the above landscape of contemporary computing?

And so such a program has been written (implemented in C++11) and it is called **spartan**.

## What is spartan?

The Linux native program **spartan** is an alternative Java program launcher. It is specifically intended to launch a Java program to run as a Linux service daemon - which means the Java program stays resident indefinitely until issued a command instructing it to exit.

The special command line option `-service` is reserved by **spartan** and instructs for the program to be run as a service daemon (yes, it overlaps with the Java JVM `-service` option but we will see how JVM options are dealt with later).

A **spartan** annotation is required to designate the `main()` method entry point for the service daemon to begin execution at.

A **spartan**-based Java program will respond to two sub commands without a programmer doing anything special:

- `status`
- `stop`

The method implementing `status` sub command can be easily overridden and customized (to display more in-depth status info).

A **spartan**-launched program can also be terminated via Control-C (Posix SIGINT signal).

Additionally **spartan** annotations can be applied to methods that are designated as entry points for programmer-defined custom sub commands.

### supervisor process and worker child processes

The **spartan** service daemon runs in a process context referred to as the *supervisor process*. It is able to easily launch *worker child processes* that execute from the very same Java program code as the supervisor.

**spartan** annotations are also used to designate method entry points for worker child processes.

A sub command can therefore be issued to invoke a worker child process from the command line (in addition to supervisor-specific sub commands already mentioned).

The **spartan** annotations for supervisor sub commands and worker child process sub commands will specify the sub command text token as an annotation attribute. That token is what is typed at the command line in order to execute it - along with any command line arguments that are passed to the sub command. Consequently **spartan**-based services are very easy to script via Linux shell programs such as `bash`.

A supervisor process can oversee the execution of one or more worker child processes - it can *supervise* them, so to speak. This is a great way to go about programming robust, self-healing software.

Multiple worker child processes could also be established with an arity corresponding to the count of detected CPU cores - the supervisor process could oversee the dispensing of work to be done by these child processes, so yet another way to partition a program for parallel processing (JVM heap size is kept to more minimal level per each parallel processing activity; the GC of each child process will be less stressed and perhaps faster due to smaller scoped garbage collections).

Native Java serialization can be effortlessly used to communicate data between these processes - they're all running from the same Java code so the classes are the same, and thus no version compatibility issues to contend with. Java serialization is also the simplest way for the supervisor to convey one-time configuration to any worker child process.

The Linux security model of the supervisor is the same for the worker child processes.

A **spartan**-based program by default does not use any TCP socket listeners - it is up to programmers to decide whether to introduce the use of any socket listeners, as determined by the domain requirements of their particular program.

**spartan** enables straightforward multi-process programming for Java programmers - read on to see why **spartan** exceeds the existing Java process-related APIs in the all crucial ease of use department.

## Why processes instead of threads only?

*Threading* came about as an evolutionary step beyond the operating system *process* as a unit of execution - a thread is a more light weight construct from a scheduler context switching perspective and thus multiple threads can be executed within a single process context (threads exist in the owning process address space). However, when designing software for high availability rigor, with self-healing capability, the *thread* is dismal by comparison to the *process* as it can too easily become catastrophically unstable and thereby render the entire Java JVM unstable.

The Java JVM runs in a single process and only has threading available by which to manage concurrent activities. When any one JVM thread becomes destabilized, the entire JVM is highly prone to being compromised too. Java thread APIs `destroy`, `stop`, `suspend`, `resume`, these are all deprecated. Here is an excerpt from the deprecated `Thread.stop` API:

> **Deprecated.** *This method is inherently unsafe. Stopping a thread with Thread.stop causes it to unlock all of the monitors that it has locked (as a natural consequence of the unchecked ThreadDeath exception propagating up the stack). If any of the objects previously protected by these monitors were in an inconsistent state, the damaged objects become visible to other threads, potentially resulting in arbitrary behavior.*

A Java thread cannot really be killed explicitly without high potential for undesirable side effects. As such java threads must be coded to terminate nicely under their own volition. If a thread becomes wedged, though, then the running service is out of luck - not much can be done for that situation. This is not the ideal for when tasked with creating software systems where high availability and self recovery are paramount concerns.

However, it is more straight forward to write robust high availability software where one process watches over another process in which the real work is being done. This arrangement is referred to as a *watchdog*. The watchdog parent process health check monitors the subsidiary child process - if the child process encounters error conditions or even out-right crashes, then the parent watchdog can take self-healing remediation actions that eventually may restore operations to normal. A wedged process or a fatally crashed child process can be explicitly killed by the parent watchdog process (or if the child process terminated abruptly the parent can detect that). A new child process can then be launched by the parent watchdog process.

In such error handling situations the cycle might continue for a while - say, if a database resource had gone off line for servicing but later was brought back online, in which case a worker child process then successfully gets a connection to the database resource and resumes operation.

Or the error condition may continue and require support staff remediation. In that event the watchdog process remains in a stable condition to where it can be monitoring the number of retries before it decides to communicate alert status to support staff. As the error condition persist the watchdog can apply back-off heuristics and perhaps spam suppression of alerts.

A watchdog coded specifically to the needs of a service can finesse the understanding of possible error conditions that might arise much more so than generic mechanisms that are typical of monitoring software packages. Having application specific knowledge can be very helpful as to error context - when retry is warranted vs. an out-right fatal condition requiring alerting, etc.

A custom implemented watchdog is the ideal because it can provide the smartest set of reactions to potential error conditions that a particular service may encounter - because it was written by a software developer that has the highest domain knowledge about the service.

### `process forking` - the easiest and best way to implement a watchdog

When programming in C or C++ (or some of the new languages such as Rust) on a Posix OS, it is possible to use the `fork` system call to spawn a child process from a parent process. This is a highly convenient mechanism for concurrent programming via processes because one can easily write the logic of the parent process and the child process(es) to reside all in single program, where said program is one executable image.

A single program using process forking will start out executing as the parent watchdog process. After making the `fork` call, it will then continue executing as the parent process along one code pathway, and then as the child process along another code pathway. It is easy to thus share the programming use of the same data structures and functions between parent and child processes.

The marshaling of data between parent and child processes will be guaranteed to be version compatible (the exact same data structures are seen identically by both) so there is no need to worry with version checks in parent-child inter-process communication.

The child process inherits and executes with the same permissions as the parent process so it can automatically access all the same files and directories that the parent can access. By the same token, the child is likewise prohibited from unwarranted resource access just as the parent. Hence the security posture is simple and straightforward - the child process can be regarded in the same manner, security-wise, as the parent process. Because of this inheriting of user identity and permissions, it is not necessary to provide authentication credentials when initiating the child process via a `fork` call.

Well, that is all nice and wonderful for C, C++, or Rust programmers, but what does that have to do with Java programming?

A core purpose of the **spartan** java program launcher is to enable a manner of programming with Java that closely resembles process forking goodness. A **spartan** programmer really does Java programming with processes but in a manner very much retaining the benefits just described.

Yes, Java already has APIs for launching other processes - but at a cost relative to `fork` call convenience and simplicity. Sometimes a major step forward in programming is a matter of making something much easier to do, with much reduction of the negative drawbacks, worries, concerns. Because of the latter, often times a facility - such as the Java API for launching processes - can go virtually unused. There is usually just too much of a hassle factor involved so programmers by and large just don't bother to go there.

## what `spartan` brings to the table

A java program launched via **spartan** has these capabilities and characteristics:

- is intended for implementing services - invoking `main(...)` initiates the program to run as a service daemon
- the service runs as the *supervisor* process
- a *supervisor* process can easily initiate a *worker child process*, which executes from the same executable program as the supervisor
- supervisor and worker child processes thus programmatically share all the same classes and methods
- a worker child process executes as the same user and has the same permissions as the supervisor
- a worker child process can i/o communicate to the supervisor via a Java `java.io.InputStream` pipe
- the supervisor can easily kill a child process and start another
- the supervisor can easily be aware of when child processes terminate (by virtue of reading from their `InputStream` pipe)
- supervisor can launch multiple child processes which can be associated to initiate their execution from different class method entry points to suit different purposes
- *sub commands* can be invoked from a command line shell (e.g. `bash`) where are trivially handled in the context of the supervisor process, or result in a child process being launched to carry out the sub command
- supervisor has automatic support of a `status` sub command - this will at a minimum list any active child processes but can be custom overridden for enhanced status info
- supervisor has automatic support of a `stop` sub command which causes the running service to cleanly exit (supervisor and any worker child processes all quit)
- spartan insures the supervisor is kept aware of child processes being started and terminated so as to keep `status` info current
- spartan really does use the `fork` system call when establishing the supervisor process and any child processes, hence spartan is currently only available on Linux
- each such process initializes its own instance of the Java JVM (a single instantiated Java JVM does not support being forked)
- the easiest way to share configuration state is for the supervisor to *Java-serialize* a configuration object to a file and then child processes deserialize that config object when starting up; in this way startup configuration processing is done just once by the service supervisor process
- of course spartan programs can be coded to dynamically retrieve config info from services such as `Consul` too, or coded to use a combination of startup config initialization combined with point-in-time dynamic config retrieval
- spartan makes use of custom annotations to denote:
    - supervisor `main` entry point
    - supervisor sub command entry points
    - worker child process sub command entry points
- spartan has the requirement that the owning class of the `main` method entry point be derived from `spartan.SpartanBase`
- the program name (as used to launch the service) is available to the supervisor `main` method entry point thread in `spartan.SpartanBase.programName` static field and via its `getProgramName()` static getter method
- the class `spartan.SpartanSysLogAppender` is derived from `ch.qos.logback.core.OutputStreamAppender<ILoggingEvent>` and enables Java code to use this logback appender to log *error* and *fatal* messages directly to the Linux syslog (no TCP port is involved - spartan directly calls Linux API for syslogging)
- the spartan convention is to use a symbolic link (having the desired program name) reference the `spartan` program launcher executable
    - spartan will look for a `config.ini` file in the same directory as the symbolic link
    - the `config.ini` can contain settings such as Java JVM settings that instantiate the JVM for the supervisor process

### `spartan` annotations

#### supervisor `main` method service entry point

The method follows the standard call signature for the Java `main` method program entry point, hence it is a static method. However, its owning class must derive from `spartan.SpartanBase` class and it must support a default constructor taking no arguments.

```java
  @SupervisorMain
  public static void main(String[] args) {
    ...
  }
```

#### supervisor *sub command* method entry point

A supervisor *sub command* method must be an instance method of the class which contains the service's `main` method entry point.

```java
  @SupervisorCommand("GENFIB")
  public void generateFibonacciSequence(String[] args, PrintStream rspStream) {
    ...
  }
```

The token name of the sub command is provided as a text string argument to the annotation. It is **spartan** programming convention to denote the sub command in upper case, but when being invoked from a shell command line it is treated in a case insensitive manner. It is okay to use an under score character but not the hyphen character in sub command tokens.

A supervisor sub command executes in the context of the supervisor process.

Code which is already executing in the supervisor process should not invoke supervisor sub commands via the **spartan** `invokeCommand` API - it should instead directly call the method. These supervisor sub commands are intended to be invoked from an external source such as from a `bash` command line shell.

The first entry of the `args` array will be the name of the sub command that was invoked.

The `PrintStream` argument is used to write output back to the invoker of the sub command.

#### worker child process *sub command* method entry point

A worker child process *sub command* method is a static method; they can reside in any class.

```java
  @ChildWorkerCommand(cmd="ETL", jvmArgs={"-Xms128m", "-Xmx324m"})
  public static void doEtlProcessing(String[] args, PrintStream rspStream) {
    ...
  }
```

At a minimum the `cmd` annotation attribute must be supplied to give the sub command token. Optionally the `jvmArgs` attribute can be present and is useful for establishing JVM options such as `-Xms` and `-Xmx` for minimum and maximum Java JVM heap memory.

A worker child process sub command executes in the context of a newly spawned child process.

The first entry of the `args` array will be the name of the sub command that was invoked.

The `PrintStream` argument is used to write output back to the invoker of the command.

A worker child process sub command can be invoked from the supervisor process via a **spartan** `invokeCommand` API; or they can be invoked from, say, a `bash` command line shell.

### some spartan APIs and class data structures

#### `spartan` API for invoking a *sub command*

Here is an example of invoking a *sub command* - the first argument is the name of the sub command, which is followed by any command line arguments (provided as text strings - just as would be the case if invoked from a `bash` command line shell).

```java
  InvokeResponse rsp = Spartan.invokeCommand("ETL", "-run-forever");
```

The class of the returned object looks like so:

```java
  class InvokeResponse {
    public final int childPID;
    public final java.io.InputStream inStream;
    public InvokeResponse(int childPID, java.io.InputStream inStream) {
      this.childPID = childPID;
      this.inStream = inStream;
    }
  }
```

The `InputStream` `inStream` field is used by the invoker to read the output generated by the sub command and to detect its termination (i.e., when the command's execution ceases for whatever reason - normal completion or even a fatally crashed child process). The `childPID` is the same pid as used in the Linux operating system to represent the spawned child process.

These **spartan** *kill* APIs can be used to terminate spawned children processes:

```java
  static void killSIGINT(int pid) throws KillProcessException;
  static void killSIGKILL(int pid) throws KillProcessException;
  
  static void killProcessGroupSIGINT(int pid) throws KillProcessException, KillProcessGroupException;
  static void killProcessGroupSIGKILL(int pid) throws KillProcessException, KillProcessGroupException;
```

An invoked sub command establishes a process group (dynamically). So `killProcessGroupSIGINT|KILL` can be used to terminate multiple spawned process instances that are executing the same sub command. The pid of any one of the process instances can be passed to the call and the entire group will be terminated.

#### `spartan` interplay with standard Linux shell commands

App support or operator staff could also use the Linux `kill` command from a command line shell to cause a worker child process to terminate:

    kill -TERM 31434

The `status` supervisor sub command can be used to produce a listing of active worker children processes:

    $ ./spartan-cfg-ex status
    spartan-cfg-ex: INFO: starting process 15968
    
        *** timestamp ***    |  *** pid *** | *** command-line ***
     2018-01-04T12:18:08.184          31434   "etl" "-run-forever" "some.json.gz"
    1 child processes active
    
    spartan-cfg-ex: INFO: process 15968 exiting normally

- In this illustration a **spartan** example program, `spartan-cfg-ex`, has at some point started running as a Linux service daemon.
- Now the **spartan** executable is indeed called `spartan`, but a symbolic link `spartan-cfg-ex` references the spartan executable, and the symbolic link name is the program name of the service (how spartan binds to the Java program to be launched will be discussed in a latter section).
- The service's supervisor process has spawned pid 31434 as a worker child process.
- The program of the service can be accessed from the command line at any time, invoking the `status` sub command, where the command's output is a listing of the active worker child processes.
- Process pid 15968 is where spartan is running in client mode. Client mode handles invoking the sub command `status` against the supervisor process; when the sub command completes then the client mode process 15968 goes away.

The point here is that a single executable program called `spartan` exist, it is a Java launcher program, but it is capable of being invoked under different context of execution, and if it is invoked with `-service` option, then it starts up a Java program running a supervisor process.

If it is invoked with a recognized sub command, then it behaves in client mode where it dispatches the sub command to be handled.

If is a supervisor sub command, then the supervisor process will handle the command and respond with any output. The client mode process prints that output to `stdout`; if there is no redirection involved then it displays on the shell console.

If the sub command is a worker child process command, spartan sees to it that a child process is spawned to carry out the command. Once again the client mode process prints any output to `stdout`.

We have already seen above that a supervisor process can use the `Spartan.invokeCommand` API call to cause a worker child process to be spawned. In that case the supervisor process will be receiving the output of the child process. The spartan client mode process was active just long enough for getting the sub command recognized and spawned as a child process then it went away.

It is very easy to now do Java programming which makes use of spawned child processes. But it is also super easy to write shell scripts that invoke sub commands and do things with their output. Thus **spartan** brings the Java programming language fully into the embrace of true Linux platform power - service daemons, command line shell interaction, shell scripts, compatible use of process pids, Linux commands (`ps`, `kill`, `top`), Linux user account for controlling permissions access, etc., etc. Everything Linux aficionados do with Linux native programs written in C or C++, they can now easily do with Java programs. In a sense, **spartan** makes Java a first class citizen of the Linux platform.

### Requirements for building `spartan`:

**Spartan** has been built and tested on RedHat/Centos distros of 6.7 and 7.4 (and Fedora 26, 27 as well as Ubuntu 14.04.5). Spartan consist of both Java and C++11 source code. It is built via the Java build tool, Maven. A Maven plugin is used to invoke `cmake`, which in turn compiles and links the C++ source code using GNU g++ compiler. Here are build provisioning prerequisites:

- **Java SDK 1.8.0**
- **Maven 3.x.x**
- **cmake 2.8.11 through cmake 3.10.0**
- **g++ 4.8.4 (C++11) through g++ 7.2.1 (C++17)** (the spartan code base is currently C++11 compliant)
- **popt-devel C library** (locate a package dialed in closely to your Linux distro version)
    - for RedHat/Centos 6.7 through 7.x:
      [popt-devel-1.13-16.el7.x86_64.rpm](https://centos.pkgs.org/7/centos-x86_64/popt-devel-1.13-16.el7.x86_64.rpm.html)
      - refer to this repository to locate other rpm packages suitable to RedHat-style Linux distros:  
        [https://pkgs.org/download/popt-devel](https://pkgs.org/download/popt-devel)
    - for Debian-style distros:  
      [libpopt-dev_1.16-8ubuntu1_amd64.deb](https://ubuntu.pkgs.org/14.04/ubuntu-main-amd64/libpopt-dev_1.16-8ubuntu1_amd64.deb.html)
      - refer to this repository to locate other deb packages suitable to Debian/Ubuntu-syle Linux distros:  
        [https://pkgs.org/download/libpopt-dev](https://pkgs.org/download/libpopt-dev)

You might choose to have the following environment variables defined in your `spartan` build context, setting directory paths appropriately to match your installation (it's possible to build `spartan` on a 512 MB VM but you may have to reduce Maven max Java heap to, say, -Xmx208m, which then leaves sufficient memory for the `cmake` portion of the build):

```shell
  export JAVA_HOME=/usr/local/java/jdk1.8.0_131
  export MAVEN_HOME=/usr/local/apache-maven-3.3.9
  export MAVEN_OPTS="-Xms256m -Xmx512m"
```

The Maven `cmake` plugin currently being used is:

```xml
  <groupId>com.googlecode.cmake-maven-project</groupId>
  <artifactId>cmake-maven-plugin</artifactId>
  <version>3.7.2-b1</version>
```

By default this plugin will locate `cmake` at `/usr/bin/cmake`. If for your distro you have to install `cmake`, it might wind up placed at a different path. The github project page of **cmake-maven-plugin** has information about customizing the plugin through configuration to use `cmake` at a different location:

[Using a local CMake installation](https://github.com/cmake-maven-project/cmake-maven-project#using-a-local-cmake-installation)

#### Java library dependencies

Spartan makes use of these open source Java libraries:

```xml
  <dependencies>
    <dependency>
      <groupId>org.javassist</groupId>
      <artifactId>javassist</artifactId>
      <version>3.20.0-GA</version>
    </dependency>
    <dependency>
      <groupId>ch.qos.logback</groupId>
      <artifactId>logback-classic</artifactId>
      <version>1.1.2</version>
    </dependency>
    <dependency>
      <groupId>ch.qos.logback</groupId>
      <artifactId>logback-core</artifactId>
      <version>1.1.2</version>
    </dependency>
  </dependencies>
```

The spartan Java API is made available per the `Spartan.jar` library, which will be located in the `spartan` installation directory. When doing a Maven build, use the `install` goal so that `Spartan.jar` will be installed in your Maven local repository, there it will be available for when building the `spartan` example programs.

### `spartan` example programs

There are three `spartan` example programs located in the `examples` sub-directory:

- **spartan-ex** : illustrates all the Spartan annotations, also shows how to do a *singleton worker child process sub command*, as well as how to use `Spartan.invokeCommand()`
- **spartan-cfg-ex** : illustrates how the *supervisor* service main thread can serialize `JCommander` object instance to a file so that worker child process sub commands can de‑serialize from that file in order to populate their `JCommander` object
- **spartan-watchdog-ex** : illustrates how to code the *supervisor* to be a watchdog over some worker child process that it runs continuously

Each is built using Maven, which produces a `.jar` file of the program in the respective `target` directory. The .jar file is what should be copied to a runtime directory; the `spartan-cfg-ex` program also requires the file `examples/spartan-cfg-ex/target/config.properties` and `jcommander-1.72.jar` to be copied to accompany it's `.jar` file.

### How to deploy and run a `spartan` example program

We will illustrate using **spartan-cfg-ex**. We will place all the programs discussed under the `/opt/` directory ([Wikipedia Filesystem Hierarchy Standard](https://en.wikipedia.org/wiki/Filesystem_Hierarchy_Standard) describes this as the location for optional or add-on software packages). The first step is to install the `spartan` launcher program.

**Tip:** *In practice, because `spartan` is intended for running Java programs as Linux services, it is recommended to create a `spartan` user and `spartan` group - deploy `spartan` into a folder to where `spartan:spartan` is set as owner/group of the folder and all its files content. Then create a user account for running the service. This user account can be be added to the `spartan` group and that group given execution permission of the `spartan` Java launcher program. Just add service user accounts to the `spartan` group as any new services are stood up. The idea is to insure each service runs in a user account by which to control permissions/access, but then share the `spartan` executable between them for Java program launching.*

Install `spartan` into an `/opt/` sub-directory:

```shell
-rw-r--r-- 1 spartan spartan   9171 Jan  4 11:19 /opt/spartan/APACHE20-LICENSE.txt
-rw-r--r-- 1 spartan spartan   1510 Jan  4 11:19 /opt/spartan/BSD-LICENSE.txt
-rw-r--r-- 1 spartan spartan    193 Jan  4 11:20 /opt/spartan/config.ini
-rw-r--r-- 1 spartan spartan 750581 Apr  9  2016 /opt/spartan/javassist-3.20.0-GA.jar
-rwxr-xr-x 1 spartan spartan 218614 Jan  4 11:20 /opt/spartan/libspartan-shared.so
-rwxr-xr-x 1 spartan spartan 260801 Jan  4 11:20 /opt/spartan/spartan
-rw-r--r-- 1 spartan spartan  59015 Jan  4 11:20 /opt/spartan/Spartan.jar
```

For ease of running the example programs, execution permission is set for all users (normally it would be preferable if only owning user and group are granted execution permission):

```shell
sudo chmod a+x /opt/spartan/spartan /opt/spartan/libspartan-shared.so
```

Now create and populate an `/opt/` sub-directory for `spartan-cfg-ex`:

```shell
lrwxrwxrwx. 1 my_user my_user    26 Jan  4 19:03 /opt/spartan-cfg-ex/spartan-cfg-ex -> /opt/spartan/spartan
-rw-r--r--. 1 my_user my_user   261 Jan  4 19:04 /opt/spartan-cfg-ex/config.ini
-rw-r--r--. 1 my_user my_user   249 Jan  4 19:06 /opt/spartan-cfg-ex/config.properties
-r--r--r--. 1 my_user my_user 69254 Jan  4 19:02 /opt/spartan-cfg-ex/jcommander-1.72.jar
-r--r--r--. 1 my_user my_user 12707 Jan  4 19:02 /opt/spartan-cfg-ex/spartan-cfg-ex.jar
```

Notice that there is a symbolic link `spartan-cfg-ex` which links to the `spartan` executable. This symbolic link and the `config.ini` file are the `spartan` conventions for launching a Java program. When the symbolic link name is invoked as the program name of the service:

```shell
./spartan-cfg-ex -service
```

the `spartan` program launcher will look in the directory where the symbolic link is located to find the file `config.ini`, where it will obtain various runtime options, such as JVM options for invoking the *supervisor process*. The `config.ini` file needs to be setup properly:

- copy `/opt/spartan/config.ini` into directory `/opt/spartan-cfg-ex/`
- run `chown` to set the owner:group of the `/opt/spartan-cfg-ex/config.ini` file appropriately
- open `config.ini` in a text editor and edit the `CommandLineArgs` property
- given above install directories and file paths, the file should end up looking like so:


```
[JvmSettings]
CommandLineArgs=-server -Xms20m -Xmx50m -Djava.class.path="/opt/spartan/Spartan.jar:/opt/spartan-cfg-ex/spartan-cfg-ex.jar" -Djava.library.path=.
[ChildProcessSettings]
ChildProcessMaxCount=30
[LoggingSettings]
LoggingLevel=INFO
```

The logging level setting here is for the `spartan` program itself and when Java code calls `Spartan.log()` API; the Java program will likely use logback, log4j, etc., for application logging in which case logging verbosity will be set through some other means for the general application logging.

Spartan currently only uses `JAVA_HOME` environment variable to locate the Java JVM shared library, so that will need to be defined appropriately in the runtime context of invoking the service.

If a user account has been established for executing the service, then the home directory could have been set as a default on that user account. However, it is possible to have multiple instances of a service being invoked to where each instance has its own home directory. The `HOME` environment variable can be defined in the context of invoking a particular instance of a service, in which case that will take precedence over any home directory associated to the service's user account.

**Tip:** *Double check that `JAVA_HOME` and `HOME` have the values that are expected within the invoked execution context of the service.*

Using the symbolic link name as the name of the program that is being run as a service has the nice effect that one can use that name with the Linux `ps` command to view its process status:

```shell
$ ps -C spartan-cfg-ex -Fww
UID        PID  PPID  C    SZ   RSS PSR STIME TTY          TIME CMD
my_user  31533  4301  0  30732 11240  0 03:24 pts/13   00:00:00 ./spartan-cfg-ex -service
my_user  31534 31533  2 494796 40132  0 03:24 pts/13   00:00:00 ./spartan-cfg-ex -service
```

The listing shows two processes but one is the parent launcher process that *fork* launched the *supervisor process*, which runs a Java JVM instance; the other is that *supervisor process* itself.

If a *worker child process sub command* has been invoked, then the `ps` listing might look something like:

```shell
$ ps -C spartan-cfg-ex -Fww
UID        PID  PPID  C    SZ   RSS PSR STIME TTY          TIME CMD
my_user  31533  4301  0  47116 11240  0 03:24 pts/13   00:00:00 ./spartan-cfg-ex -service
my_user  31534 31533  0 496845 43044  0 03:24 pts/13   00:00:01 ./spartan-cfg-ex -service
my_user  31595  4325  0   6109  3804  0 03:30 pts/14   00:00:00 ./spartan-cfg-ex cdcetl -run-forever some.json.gz
my_user  31596 31533  3 563971 35620  0 03:30 pts/13   00:00:00 ./spartan-cfg-ex -service
```

The process pid 31595 in this case is `spartan` invoked in client mode in order to handle launching a worker child process - the command line to the sub command `cdcetl` is displayed here. The actual worker child process is pid 31596, which has another instantiated Java JVM instance. The `-service` option appearing on the JVM child process is just an artifact of the `fork()` call being utilized, but in actuality that child process received the command line arguments it was invoked with.

Presuming that the service is running foreground in one terminal console, then by opening another terminal, the command to see application status can be issued (notice the use of `sudo` to invoke the command as the same user the service is running as):

```shell
$ sudo -u my_user /opt/spartan-cfg-ex/spartan-cfg-ex status
spartan-cfg-ex: INFO: starting process 31639

    *** timestamp ***    |  *** pid *** | *** command-line ***
 2018-01-29T03:56:49.440          31596   "cdcetl" "-run-forever" "some.json.gz"
1 child processes active

spartan-cfg-ex: INFO: process 31639 exiting normally
```

The pid 31639 is `spartan` as invoked in client mode (the client mode process handles the output generated by the sub command by echoing it to `stdout`).

Then to terminate the service, which will cause all processes to exit, can issue the *supervisor* `stop` sub command:

```shell
$ sudo -u my_user /opt/spartan-cfg-ex/spartan-cfg-ex stop
```

As mentioned previously, the `kill -TERM` command can be used to cause a worker child process to exit without effecting the supervisor process of the service - just specify the child process pid number to the `kill` command as it is seen displayed in the `status` listing. Run the `status` command again and it will be seen that the child process has gone away.

This command line will cause a singleton worker child process to be launched:

```shell
$ sudo -u my_user /opt/spartan-cfg-ex/spartan-cfg-ex cdcetl -run-forever some.json.gz
```

**Tip:** *Keep in mind that if file paths are passed as arguments to a sub command, the user that the service runs as will need to have access permission established to that file. The example programs are not taking real files as arguments - they only serve for illustrating passing various arguments to sub commands.*

A `spartan` *singleton* refers to a worker child process sub command that is restricted to being run as a single process at any given time. The `Spartan.isFirstInstance()` API is used to establish a fence around the core activity of the singleton sub command.

The normal posture is that multiple instances of a worker child process sub command can be invoked to run concurrently. The sub command `genetl` is an example of a command that can be invoked to run many times concurrently; can try it out by using the same command line as above but change `cdcetl` to `genetl` (will have to start another terminal to run each sub command, or else use a utility such as GNU Screen). All of these child processes will be listed by the `status` command - `spartan` keeps track of when these child processes are launched and when they terminate so the status stays updated automatically.

**NOTE:** *Currently the `spartan` client mode does not respond to Control-C - the child process at the other end of the pipe it is reading from can instead be caused to terminate by using `kill -TERM` on its pid, which the client mode will sense as the pipe will indicate no more input to be read.*

## `spartan` road map - a hat tip to reactive programming

Spartan has been in production use for over a year, but its development began back in 2015. It originated when C++11 and Java 8 were new. Now C++17 and Java 10 are the latest. In the Java community, the Jetbrains Kotlin language has been making an impact (with quite the boost from Google's adoption of Kotlin for Android programming).

- For the time being the `spartan` C++11 code base will remain compileable in g++ 4.8.2 as that has been sufficient for its functionality.
- It's possible that `spartan` might support Kotlin as is but no attempt has been made to try it out yet (not using Kotlin in the day job so hasn't been a priority).
- Java 8 is the only Java language version supported so far. Yet Java 10 is here now and will definitely be important to support. That will require a new round of significant development and testing effort (making sure to support Java modularity introduced in Java 9).
- Implement Control-C (SIGINT signal) handling for the `spartan` client mode
    - *it needs to convey SIGTERM to child process at other end of pipe, or if is reading from the supervisor, should just break off reading its output*
- Miscellaneous improvements (relatively easy enhancements):
    - provide for special variables that can be used in the `config.ini` file to denote the `spartan` install directory path and the directory path where the symbolic link is located
    - `spartan` private environment variable `SPARTAN_JAVA_HOME` that will take precedence over `JAVA_HOME` if it is defined
- Spartan Java API to allow for authentication as a Linux user
    - *current security model is pretty simple - this prys the door open to greater complexity, so very iffy on this feature*
- Addition of a new, enhanced, reactive programming API, loosely based on the Java-9-introduced `java.util.concurrent.Flow` interfaces. This reactive API is specifically for invoking worker child process sub commands. A new `invoke` method will return an extended `InvokeResponseEx` object:

```java
  class InvokeResponse {
    public final int childPID;
    public final java.io.InputStream inStream;
    InvokeResponse(int childPID, java.io.InputStream inStream) {
      this.childPID = childPID;
      this.inStream = inStream;
    }
  }

  final class InvokeResponseEx extends InvokeResponse {
    public final InputStream errStream;
    public final OutputStream childInputStream;
    InvokeResponseEx(int childPID, InputStream inStream, InputStream errStream, OutputStream childInputStream) {
      super(childPID, inStream);
      this.errStream = errStream;
      this.childInputStream = childInputStream;
    }
  }

  static InvokeResponseEx invokeCommandEx(String... args) { ... }
```

Instead of the class inheritance/interface programming model that `java.util.concurrent.Flow` mandates (concrete classes must be created which implement the `Flow` interfaces such as `Subscriber`), the **spartan** approach is a lambda-styled API. Here are proposed interfaces for `spartan.fstreams.Flow`:

```java
public final class Flow {
  public interface Subscriber {
    Subscriber onError(BiConsumer<InputStream, Subscription> onErrorAction);
    Subscriber onNext(BiConsumer<InputStream, Subscription> onNextAction);
    Subscriber subscribe(InvokeResponseEx rsp);
    FuturesCompletion start();
  }

  public interface Subscription {
    void cancel();
    OutputStream getRequestStream();
  }

  public interface FuturesCompletion {
    Future<Integer> poll();
    Future<Integer> poll(long timeout, TimeUnit unit) throws InterruptedException;
    Future<Integer> take() throws InterruptedException;
    int count();
  }

  public static Subscriber subscribe(InvokeResponseEx rsp) { ... }
}
```

Here is an example of how to program to this lambda-oriented reactive API:

```java
  InvokeResponseEx rsp = Spartan.invokeCommandEx("ETL", dataInputFile.getPath());

  final OutputStream errOutFileStream = Files.newOutputStream(errOutFile.toPath(), CREATE, TRUNCATE_EXISTING);
  final OutputStream outputFileStream = Files.newOutputStream(outputFile.toPath(), CREATE, TRUNCATE_EXISTING);

  FuturesCompletion futures = spartan.fstreams.Flow.subscribe(rsp)
      .onError((errStrm, subscription) -> copyWithClose(errStrm, errOutFileStream, subscription))
      .onNext((outStrm,  subscription) -> copyWithClose(outStrm, outputFileStream, subscription))
      .start();

  int count = futures.count();
  while(count-- > 0) {
    try {
      int childPID = futures.take().get();
    } catch (ExecutionException e) {
      log.error("exception encountered in sub task:", e);
    } catch (InterruptedException e) {
      log.warn("interruption occurred");
    }
  }
```

Multiple child worker subprocesses can be invoked and then their respective subscriptions chained into a single `FuturesCompletion` context:

```java
  InvokeResponseEx rsp1 = Spartan.invokeCommandEx("FIB_GEN", "1000000");
  InvokeResponseEx rsp2 = Spartan.invokeCommandEx("EXTRACT", sourceFile.getPath());

  FuturesCompletion futures = spartan.fstreams.Flow.subscribe(rsp1)
      .onError((errStrm, subscription) -> copyWithClose(errStrm, errOutFileStream01, subscription))
      .onNext((outStrm,  subscription) -> copyWithClose(outStrm, outputFileStream01, subscription))
      .subscribe(rsp2)
      .onError((errStrm, subscription) -> copyWithClose(errStrm, errOutFileStream02, subscription))
      .onNext((outStrm,  subscription) -> copyWithClose(outStrm, outputFileStream02, subscription))
      .start();

  int count = futures.count();
  while(count-- > 0) {
    try {
      int childPID = futures.take().get();
    } catch (ExecutionException e) {
      log.error("exception encountered in sub task:", e);
    } catch (InterruptedException e) {
      log.warn("interruption occurred");
    }
  }
```

Or the subscribers could be chained via loop iteration - here we see subscribers established for a list of input files (where each input file is passed to a child worker process via `invokeCommandEx(...)`):

```java
  final Set<Integer> childPIDs = new HashSet<>();
  Subscriber subscriber = null;

  for(final File inputFile : inputFiles) {
    final String inputFileName = inputFile.getName();
    final int lastIndex = inputFileName.lastIndexOf(".gz");
    final String outputFileName = inputFileName.substring(0, lastIndex);
    final File errOutFile = new File(inputFile.getParent(), outputFileName + ".err");
    final File outputFile = new File(inputFile.getParent(), outputFileName);
    final OutputStream errOutFileStream = Files.newOutputStream(errOutFile.toPath(), CREATE, TRUNCATE_EXISTING);
    final OutputStream outputFileStream = Files.newOutputStream(outputFile.toPath(), CREATE, TRUNCATE_EXISTING);
    final InvokeResponseEx rsp = Spartan.invokeCommandEx("UN_GZIP", inputFile.getPath());
    childPIDs.add(rsp.childPID);
    subscriber = subscriber == null ? spartan.fstreams.Flow.subscribe(rsp) : subscriber.subscribe(rsp);
    subscriber
        .onError((errStrm, subscription) -> copyWithClose(errStrm, errOutFileStream, subscription))
        .onNext((outStrm,  subscription) -> copyWithClose(outStrm, outputFileStream, subscription));
  }

  final FuturesCompletion futures = subscriber.start();

  int count = futures.count();
  final String[] pids = new String[count];
  int i = 0;

  while(count-- > 0) {
    try {
      final Integer childPID = futures.take().get();
      childPIDs.remove(childPID);
      pids[i++] = childPID.toString();
    } catch (ExecutionException e) {
      log.error("exception encountered in sub task:", e);
    } catch (InterruptedException e) {
      log.warn("interruption occurred");
    }
  }
```

Notice that in the first iteration pass through the loop the subscriber is established via the static method `Flow.subscribe(...)` call and then thereafter via the instance method `subscribe(...)` call.

The new `Flow` interfaces and the above depicted style of programming have already been proved out in a prototyping test bed application.

This feature will also require the addition of a new annotation which will be associated to a new method entry point signature for child worker subcommands. The method signature will have two additional stream arguments like so:

```java
  @ChildWorkerCommandEx(cmd="CDCETL", jvmArgs={"-Xms128m", "-Xmx324m"})
  public static void doCdcEtlProcessing(String[] args, PrintStream outStream, PrintStream errStream, InputStream inStream) { ... }
```

Also, the new `Spartan.invokeCommandEx(...)` method still remains to be implemented - it will require support in the underlying C++ **spartan** program launcher.

Working with the lambda-centric **spartan** `Flow` interfaces is rather eye opening in itself - it is a much more accomodating programming model than the `Flow` interfaces programming model introduced in Java 9. Frankly, that was the old style of Java programming that predates Java 8 and it is unfortunate that reactive programming for Java, as introduced in Java 9, was based on this (the reactive programming standard it is adhering to is just plain out of date in respect to contemporary Java programming practices).

One thing that will be noticed is that the **spartan** `Flow` interfaces deal with `InputStream` and `OutputStream`. The intent is to allow direct use of the stream communication pipes to the child worker process. The Java 9 `Flow` approach uses generic type templating and passes materialized objects to a subscriber. My personal use of **spartan** child worker processes to date is such that their output is consumed by a special zero-garbage text line reader class in combination with regular expressions acting on a `CharSequence` buffer, and which match some simple command language syntax. Consequently a child worker process can be monitored by a supevisor in an infinitely running 24/7 manner and yet generate virtually no heap garbage - a desirable characteristic.

An object serialization abstraction layer could further be devised on top of this API, though, if one preferred to work with Java objects instead. 

## Conclusion

In other programming languages what **spartan** empowers would perhaps not be very special. Concurrent programming via processes is a rather ancient practice in computing. All manner of stalwart programs that run as services on Linux, like PostreSQL, MySQL, Redis (many others) - and desktop applications such as Chrome browser and lately the Firefox brower - utilize multi-process concurrent programming as core to how they are designed and operate.

However, for the Java language, where this manner of programming now becomes so facile, its very much like a new landscape of program architecture is presenting itself. Enjoy.