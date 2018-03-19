/* App.java

Copyright 2016 - 2018 Tideworks Technology
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
package com.tideworks.test;

import static java.lang.String.format;

import java.io.File;
import java.io.PrintStream;
import java.net.MalformedURLException;
import java.net.URISyntaxException;
import java.net.URL;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.List;
import java.util.function.Function;
import java.util.function.Supplier;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Test program for testing Spartan logback appender that
 * writes to the Linux syslog API.
 */
public class App extends spartan.SpartanBase {
  private static final String clsName = App.class.getSimpleName();
  private static final String progname;
  private static final File progDirPath;
  private static Logger logger;

  /**
   * Class static initializer - sets static fields:
   * <p>
   *   progname
   *   progDirPath
   */
  static {
    final Function<URL,URL> determineJarUrl = (url) -> {
      try {
        return new URL(url.getPath().split("!/")[0]);
      } catch (MalformedURLException ex) {
        throw new RuntimeException(ex);
      }
    };

    final Function<URL,Path> getJarFilePath = (url) -> {
      try {
        return Paths.get(url.toURI());
      } catch (URISyntaxException ex) {
        throw new RuntimeException(ex);
      }
    };

    final Function<Class<?>,File> prog_dir_path = (Class<?> cls) -> {
      final URL url = cls.getResource(cls.getSimpleName() + ".class");
      final URL jarUrl = determineJarUrl.apply(url);
      final Path jarPath = getJarFilePath.apply(jarUrl);
      return jarPath.toFile().getParentFile();
    };

    final Function<Class<?>,Class<?>> getAndConfirmClassAsMainThread = cls -> {
      final String trgClsName = cls.getName();
      final StackTraceElement[] stack = Thread.currentThread().getStackTrace();
      for(int i = stack.length - 1; i >= 0; i--) {
        final StackTraceElement stackElem = stack[i];
        final String clsName = stackElem.getClassName();
        if (clsName.equals(trgClsName)) {
          return cls;
        }
      }
      throw new RuntimeException(format("%s not found in runtime stack", trgClsName));
    };

    String mainClass = null;
    File dirpath = null;
    try {
      final Class<?> cls = getAndConfirmClassAsMainThread.apply(App.class);
      mainClass = cls.getSimpleName();
      dirpath = prog_dir_path.apply(cls);
    } catch (RuntimeException e) {
      final Throwable cause = e.getCause();
      if (cause != null) {
        cause.printStackTrace(System.err);
      } else {
        e.printStackTrace(System.err);
      }
      System.exit(1);
    } catch(Throwable e) {
      e.printStackTrace(System.err);
      System.exit(1);
    }

    progname = mainClass;
    progDirPath = dirpath;
    logger = effectLoggingLevel(() -> LoggerFactory.getLogger(progname));
  }

  private static Logger effectLoggingLevel(final Supplier<Logger> createLogger) {
    String logbackCfgFileName = "logback.xml";
    File logbackCfgFile = new File(progDirPath, logbackCfgFileName);
    if (!logbackCfgFile.exists()) {
      System.err.printf("LogBack config file \"%s\" not detected - defaulting to console logging%n",logbackCfgFileName);
      System.err.printf("Expected LogBack config file full pathname:%n\t\"%s\"%n", logbackCfgFile);
    }
    System.setProperty("logback.configurationFile", logbackCfgFile.toString());
    System.setProperty("program.directoryPath", progDirPath.toString());
    ch.qos.logback.classic.Logger root = (ch.qos.logback.classic.Logger) LoggerFactory
        .getLogger(ch.qos.logback.classic.Logger.ROOT_LOGGER_NAME);
    root.setLevel(ch.qos.logback.classic.Level.toLevel("DEBUG"));
    return createLogger.get();
  }


  public static void main( String[] args ) {
    System.out.println( "Hello World!" );
    // Blocks indefinitely here - when service shuts down then
    // an OS signal will cause program to unblock and exit.
    // (Will be able to respond to supervisor commands.)
    enterSupervisorMode();
  }

  protected static void enterSupervisorMode() {
    // Blocks indefinitely here - when service shuts down then
    // an OS signal will cause program to unblock and exit.
    try {
      _lock.lock();
      _condition.await();
    } catch (InterruptedException e) {
    } finally {
      _lock.unlock();
    }
  }

  @Override
  public void supervisorDoCommand(String[] args, PrintStream rspStream) {
    final String methodName = "supervisorDoCommand";
    logger.debug(">> {}.{}(args count:{})", clsName, methodName, args.length);

    try (final PrintStream rsp = rspStream) {
      final List<String> argsLst = args.length > 1 ? Arrays.asList(args).subList(1, args.length) : null;
      final String[] cmdArgs = argsLst != null ? argsLst.toArray(new String[argsLst.size()]) : new String[0];
      final String cmdLine = String.join(" ", cmdArgs);
      // just echo back the command line
      final String msg = format("%s: %s", args[0].toUpperCase(), cmdLine);
      rsp.println(msg);
      logger.error(msg); // should be logged to Linux syslog in addition to logback file appender target
    }

    logger.debug("<< {}.{}()", clsName, methodName);
  }
}
