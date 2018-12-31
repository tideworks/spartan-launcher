/* CommandDispatchInfo.java

Copyright 2016 - 2017 Tideworks Technology
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

import static java.lang.String.format;
import static spartan.util.EnumAsStream.enumerationAsStream;

import java.io.ByteArrayInputStream;
import java.io.DataInputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.ObjectInputStream;
import java.io.ObjectOutputStream;
import java.io.OutputStream;
import java.io.Serializable;
import java.net.MalformedURLException;
import java.net.URISyntaxException;
import java.net.URL;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Enumeration;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Properties;
import java.util.Set;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.function.Predicate;
import java.util.function.Supplier;
import java.util.jar.Attributes;
import java.util.jar.JarEntry;
import java.util.jar.JarInputStream;
import java.util.jar.Manifest;
import java.util.stream.Stream;

import javassist.bytecode.AnnotationsAttribute;
import javassist.bytecode.ClassFile;
import javassist.bytecode.MethodInfo;
import javassist.bytecode.annotation.Annotation;
import javassist.bytecode.annotation.ArrayMemberValue;
import javassist.bytecode.annotation.MemberValue;
import javassist.bytecode.annotation.StringMemberValue;
import spartan.annotations.ChildWorkerCommand;
import spartan.annotations.SupervisorCommand;
import spartan.annotations.SupervisorMain;
import spartan.util.io.ByteArrayOutputStream;

/**
 * This class will be populated by Java code running in JVM,
 * but a populated instance will be consumed by C++ code
 * which uses JNI APIs to access fields (even though private).
 * <p>
 * This class is not intended for consumption by other Java code.
 */
public final class CommandDispatchInfo implements Serializable {
  private static final long serialVersionUID = 1L;
  private static final String eol = System.getProperty("line.separator");
  private static final Set<String> spartanAnnotations;
  private static final Set<String> spartanAnnotationValidMetaData;
  private static int loggingLevel = 0;

  static {
    spartanAnnotations = new HashSet<>();
    spartanAnnotations.add(SupervisorMain.class.getName());
    spartanAnnotations.add(SupervisorCommand.class.getName());
    spartanAnnotations.add(ChildWorkerCommand.class.getName());
    spartanAnnotationValidMetaData = new HashSet<>();
    spartanAnnotationValidMetaData.add("value");
    spartanAnnotationValidMetaData.add("cmd");
    spartanAnnotationValidMetaData.add("jvmArgs");
  }

  // these private fields will be accessible to C++ code via JNI APIs
  private File jarsDir;
  private String[] manifestClassPath = new String[0];
  private String[] systemClassPath = new String[0];
  private MethInfo spartanMainEntryPoint;
  private CmdInfo[] spartanSupervisorCommands = new CmdInfo[0];
  private ChildCmdInfo[] spartanChildWorkerCommands = new ChildCmdInfo[0];


  /**
   * Exceptions derived from {@link Throwable}, especially {@link Exception} derived
   * classes, are re-thrown as unchecked.<p/>
   * <i>Rethrown exceptions are not wrapped yet the compiler does not detect them as
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

  protected static void setDebugLoggingLevel() {
    loggingLevel = spartan.Spartan.LL_DEBUG;
  }
  private static void logF(Supplier<String> msg) {
    if (loggingLevel != spartan.Spartan.LL_DEBUG) return;
    System.out.print(msg.get());
  }
  private static void logLn(Supplier<String> msg) {
    if (loggingLevel != spartan.Spartan.LL_DEBUG) return;
    System.out.println(msg.get());
  }

  @Override
  public String toString() {
    final StringBuilder sb = new StringBuilder(2048).append(this.getClass().getSimpleName()).append(':').append(eol)
        .append("  jarsDir: ").append(jarsDir.toString()).append(eol);
    sb.append("  manifestClassPath: ");
    for(final String classpathElm : manifestClassPath) {
      sb.append(classpathElm).append(java.io.File.pathSeparatorChar);
    }
    sb.delete(sb.length() - 1, sb.length()).append(eol);
    sb.append("  systemClassPath: ");
    for(final String classpathElm : systemClassPath) {
      sb.append(classpathElm).append(java.io.File.pathSeparatorChar);
    }
    sb.delete(sb.length() - 1, sb.length()).append(eol);
    sb.append("  spartanMainEntryPoint:").append(eol).append(spartanMainEntryPoint);
    sb.append("  spartanSupervisorCommands:").append(eol);
    for(final CmdInfo cmdInfo : spartanSupervisorCommands) {
      sb.append(cmdInfo);
    }
    sb.append("  spartanChildWorkerCommands:").append(eol);
    for(final ChildCmdInfo childCmdInfo : spartanChildWorkerCommands) {
      sb.append(childCmdInfo);
    }
    return sb.toString();
  }

  public static class MethInfo implements Serializable {
    private static final long serialVersionUID = 1L;
    // these private fields will be accessible to C++ code via JNI APIs
    private final String className;
    private final String methodName;
    private final String descriptor;
    public MethInfo(String className, String methodName, String descriptor) {
      this.className = className;
      this.methodName = methodName;
      this.descriptor = descriptor;
    }
    protected StringBuilder toStringBuilder() {
      return new StringBuilder(512).append("    ").append(this.getClass().getSimpleName()).append(':').append(eol)
          .append("      className: ").append(className).append(eol)
          .append("      methodName: ").append(methodName).append(eol)
          .append("      descriptor: ").append(descriptor).append(eol);
    }
    @Override
    public String toString() { return toStringBuilder().toString(); }
  }
  public static class CmdInfo extends MethInfo {
    private static final long serialVersionUID = 1L;
    // these private fields will be accessible to C++ code via JNI APIs
    protected String cmd;
    public void setCmd(String cmd) {
      this.cmd = cmd;
    }
    public CmdInfo(String className, String methodName, String descriptor) {
      super(className, methodName, descriptor);
      this.cmd = "";
    }
    @Override
    public String toString() {
      return toStringBuilder().append("      cmd: ").append(cmd).append(eol).toString();
    }
  }
  public static class ChildCmdInfo extends CmdInfo {
    private static final long serialVersionUID = 1L;
    // these private fields will be accessible to C++ code via JNI APIs
    private String[] jvmArgs;
    public void setJvmArgs(String[] jvmArgs) {
      this.jvmArgs = jvmArgs;
    }
    public String getJvmOptionsCommandLine() {
      return String.join(" ", jvmArgs);
    }
    public ChildCmdInfo(String className, String methodName, String descriptor) {
      super(className, methodName, descriptor);
      this.jvmArgs = new String[0];
    }
    @Override
    public String toString() {
      final StringBuilder sb = toStringBuilder()
          .append("      cmd: ").append(cmd).append(eol)
          .append("      jvmArgs: ");
      for(final String jvmArg : jvmArgs) {
        sb.append(jvmArg).append(' ');
      }
      return sb.delete(sb.length() - 1, sb.length()).append(eol).toString();
    }
  }

  public static byte[] obtainSerializedSysPropertiesAndAnnotationInfo()
      throws MalformedURLException, URISyntaxException, IOException
  {
    final CommandDispatchInfo info = obtainAnnotationInfo();
    final ByteArrayOutputStream byteStream = new ByteArrayOutputStream(1024 * 8);
    try (final OutputStream outStrm = byteStream) {
      try(final ObjectOutputStream out = new ObjectOutputStream(outStrm)) {
        final Properties sysProps = System.getProperties();
        out.writeObject(sysProps);
        out.writeObject(info);
      }
    }
    return byteStream.toByteArray();
  }

  public static void deserializeAndSetSystemProperties(byte[] byteSerializedData)
      throws IOException, ClassNotFoundException
  {
    final ByteArrayInputStream inputStream = new ByteArrayInputStream(byteSerializedData);
    try (final ObjectInputStream in = new ObjectInputStream(inputStream)) {
      final Properties sysProps = (Properties) in.readUnshared();
      System.setProperties(sysProps);
    }
  }

  public static String[] deserializeSystemProperties(byte[] byteSerializedData)
      throws IOException, ClassNotFoundException
  {
    final ByteArrayInputStream inputStream = new ByteArrayInputStream(byteSerializedData);
    try (final ObjectInputStream in = new ObjectInputStream(inputStream)) {
      final Properties sysProps = (Properties) in.readUnshared();
      final String[] rslt = new String[sysProps.size()];
      int i = 0;
      for (final Map.Entry<Object, Object> e : sysProps.entrySet()) {
        rslt[i++] = String.join("=", e.getKey().toString(), e.getValue().toString());
      }
      return rslt;
    }
  }

  public static void setSystemProperties(String[] propStrs) {
    if (propStrs == null || propStrs.length <= 0) return;
    final Properties sysProps = new Properties();
    for(final String line : propStrs) {
      final String[] parts = line.split("=", 2);
      sysProps.setProperty(parts[0], parts[1]);
    }
    System.setProperties(sysProps);
  }

  public static CommandDispatchInfo deserializeToAnnotationInfo(byte[] byteSerializedData)
      throws ClassNotFoundException, IOException
  {
    final ByteArrayInputStream inputStream = new ByteArrayInputStream(byteSerializedData);
    try (final ObjectInputStream in = new ObjectInputStream(inputStream)) {
      final Object sysProps = in.readObject(); // skip over the system properties collection
      assert(sysProps instanceof Properties);
      return (CommandDispatchInfo) in.readObject(); // return the annotation info
    }
  }

  public static CommandDispatchInfo obtainAnnotationInfo()
      throws MalformedURLException, URISyntaxException, IOException
  {
    final ClassLoader clsLdr = Thread.currentThread().getContextClassLoader();
    final Enumeration<URL> urls = clsLdr.getResources("META-INF/MANIFEST.MF"); // find jar files
    final Stream<URL> urlsStream = enumerationAsStream(urls);
    return new CommandDispatchInfo().obtainAnnotationInfo(clsLdr, urlsStream);
  }

  private static String[] getSysClassPathArray() {
    final String currSysClassPath = System.getProperty("java.class.path");
    if (currSysClassPath != null && !currSysClassPath.isEmpty()) {
      final String separator = format("\\s*%s\\s*", java.io.File.pathSeparator);
      return currSysClassPath.split(separator);
    }
    return new String[0];
  }

  private CommandDispatchInfo obtainAnnotationInfo(final ClassLoader clsLdr, final Stream<URL> urlsStream)
      throws MalformedURLException, URISyntaxException, IOException
  {
    final CommandDispatchInfo self = new AnnotationScanner().obtainAnnotationInfo(urlsStream);
    assert(self == this);

    final Set<Path> classPathSet = new LinkedHashSet<>();
    final ArrayList<String> jarPaths = new ArrayList<>();

    final Runnable processCurrSysClassPath = () -> {
      final String[] currSysClassPathArray = getSysClassPathArray();
      for(final String currSysClassPathElm : currSysClassPathArray) {
        final Path currSysClassPathElmFilePath = new File(currSysClassPathElm).toPath();
        if (classPathSet.add(currSysClassPathElmFilePath.getFileName())) {
          jarPaths.add(currSysClassPathElm);
        }
      }
    };

    if (spartanMainEntryPoint != null) {
      final URL url = clsLdr.getResource(spartanMainEntryPoint.className.replace('.', '/') + ".class");
      final URL jarUrl = determineJarUrl.apply(url);
      final Path jarPath = getJarFilePath.apply(jarUrl);
      classPathSet.add(jarPath.getFileName());
      jarPaths.add(jarPath.toString());
      self.jarsDir = jarPath.toFile().getParentFile();
      final InputStream jarStream = getInputStream.apply(jarPath);
      try(final JarInputStream jarFile = new JarInputStream(jarStream)) {
        final Manifest manifest = jarFile.getManifest();
        if (manifest != null) {
          final Attributes mainAttributes = manifest.getMainAttributes();
          if (mainAttributes != null) {
            final String jarClassPath = mainAttributes.getValue("Class-Path");
            if (jarClassPath != null) {
              self.manifestClassPath = jarClassPath.split("\\s+");
              jarPaths.ensureCapacity(self.manifestClassPath.length + 1);
              Arrays.stream(self.manifestClassPath)
                .filter(Objects::nonNull)
                .map(e -> new File(self.jarsDir, e))
                .map(File::toPath)
                .forEach(e -> {
                  if (classPathSet.add(e.getFileName())) {
                    jarPaths.add(e.toString());
                  }
                });
            }
          }
        }
      }
    }

    self.manifestClassPath = jarPaths.toArray(new String[jarPaths.size()]);
    processCurrSysClassPath.run();
    self.systemClassPath = jarPaths.toArray(new String[jarPaths.size()]);
    final String newClassPath = String.join(java.io.File.pathSeparator, jarPaths);
    System.setProperty("java.class.path", newClassPath);
    logF(()->format("java.class.path: %s%n", System.getProperty("java.class.path")));

    return self;
  }

  private static final Function<URL,URL> determineJarUrl = (url) -> {
    URL rslt = null;
    try {
      rslt = new URL(url.getPath().split("!/")[0]);
    } catch (MalformedURLException ex) {
      uncheckedExceptionThrow(ex);
    }
    return rslt;
  };

  private static final Function<URL,Path> getJarFilePath = (url) -> {
    Path rslt = null;
    try {
      rslt = Paths.get(url.toURI());
    } catch (URISyntaxException ex) {
      uncheckedExceptionThrow(ex);
    }
    return rslt;
  };

  private static final Function<Path,InputStream> getInputStream = (path) -> {
    InputStream rslt = null;
    try {
      rslt = Files.newInputStream(path, StandardOpenOption.READ);
    } catch (IOException ex) {
      uncheckedExceptionThrow(ex);
    }
    return rslt;
  };

  private final class AnnotationScanner {
    private final ArrayList<CmdInfo> spartanSupervisorCmds = new ArrayList<>();
    private final ArrayList<ChildCmdInfo> spartanChildWorkerCmds = new ArrayList<>();
  
    private synchronized CommandDispatchInfo obtainAnnotationInfo(final Stream<URL> urlsStream)
        throws MalformedURLException, URISyntaxException, IOException
    {
      final Consumer<InputStream> processJarStream = (jarStream) -> {
        try(final JarInputStream jarFile = new JarInputStream(jarStream)) {
          try(final ByteArrayOutputStream sink = new ByteArrayOutputStream(4096)) {
            final byte[] iobuf = new byte[512];
            JarEntry jarEntry;
            while ((jarEntry = jarFile.getNextJarEntry()) != null) {
              String jarEntryName = jarEntry.getName();
              if (!jarEntry.isDirectory() && jarEntryName.endsWith(".class")) {
//                log(()->format("%s: size:%d%n", jarEntryName, jarEntry.getSize()));
                for(int n = 0; n != -1;) {
                  n = jarFile.read(iobuf, 0, iobuf.length);
                  if (n > 0) {
                    sink.write(iobuf, 0, n);
                  }
                }
                sink.flush();
                final ClassFile cf = new ClassFile(
                    new DataInputStream(new ByteArrayInputStream(sink.getBuffer(), 0, sink.size())));
                scanMethods(cf);
              }
              sink.reset();
            }
          }
        } catch(Exception ex) {
          uncheckedExceptionThrow(ex);
        }
      };

      urlsStream
        .map(determineJarUrl)       // determine URL to jar file
        .map(getJarFilePath)        // convert URL->URI->Path per jar file
        .map(getInputStream)        // convert file Path to an opened InputStream
        .forEach(processJarStream); // process each .class entry per jar file

      int size = this.spartanSupervisorCmds.size();
      if (size > 0) {
        final CmdInfo[] cmdInfoArray = new CmdInfo[size];
        this.spartanSupervisorCmds.toArray(cmdInfoArray);
        CommandDispatchInfo.this.spartanSupervisorCommands = cmdInfoArray;
        this.spartanSupervisorCmds.clear();
      }
      size = this.spartanChildWorkerCmds.size();
      if (size > 0) {
        final ChildCmdInfo[] childCmdInfoArray = new ChildCmdInfo[size];
        this.spartanChildWorkerCmds.toArray(childCmdInfoArray);
        CommandDispatchInfo.this.spartanChildWorkerCommands = childCmdInfoArray;
        this.spartanChildWorkerCmds.clear();
      }
  
      return CommandDispatchInfo.this;
    }

    /**
    * Scans both the method for annotations.
    *
    * @param cf class file to be scanned for annotations
    */
    private void scanMethods(final ClassFile cf) {
      final Consumer<MethodInfo> handleMethod = method -> {
        AnnotationsAttribute visible = (AnnotationsAttribute) method.getAttribute(AnnotationsAttribute.visibleTag);
        if (visible != null) {
          populate(visible.getAnnotations(), cf.getName(), method);
        }
      };
      @SuppressWarnings("unchecked")
      final List<MethodInfo> methods = cf.getMethods();
      if (methods != null) {
        methods.stream()
          .filter(Objects::nonNull)
          .forEach(handleMethod);
      }
    }

    private void handleAnnotationValue(final Annotation annotation, final String valueItem, final CmdInfo cmdInfo) {
      final MemberValue mVal = annotation.getMemberValue(valueItem);
      if (mVal instanceof StringMemberValue) {
        cmdInfo.setCmd(((StringMemberValue) mVal).getValue());
        logF(()->format("\t\t%s{%s}: %s%n", valueItem, String.class.getSimpleName(), mVal));
      } else if (mVal instanceof ArrayMemberValue) {
        final MemberValue[] mVals = ((ArrayMemberValue) mVal).getValue();
        if (mVals != null && mVals.length > 0 && mVals[0] != null) {
          final ChildCmdInfo childCmdInfo = (ChildCmdInfo) cmdInfo;
          final String[] jvmArgs = new String[mVals.length];
          int i = 0;
          for(final MemberValue val : mVals) {
            jvmArgs[i++] = val instanceof StringMemberValue ? ((StringMemberValue) val).getValue() : val.toString();
          }
          childCmdInfo.setJvmArgs(jvmArgs);
          final String valType = mVals[0] instanceof StringMemberValue ?
              String.class.getSimpleName() : mVals[0].getClass().getSimpleName();
              logF(()->format("\t\t%s{%s[]}: %s%n", valueItem, valType, mVal));
        } else {
          logF(()->format("\t\t%s{???[]}: %s%n", valueItem, mVal));
        }
      } else {
        logF(()->format("\t\t%s: %s%n", valueItem, mVal));
      }
    }

    private void populate(final Annotation[] annotations, final String className, final MethodInfo method) {

      final AtomicBoolean isNonSpartanTestSupervisorMain  = new AtomicBoolean();
      final AtomicBoolean isNonSpartanTestSupervisorCmd   = new AtomicBoolean();
      final AtomicBoolean isNonSpartanTestChildWorkerCmd  = new AtomicBoolean();

      final Predicate<MethInfo> isNullOrSpartanTestMethod = methInfo -> {
        return methInfo == null || methInfo.className.startsWith("spartan.test")
            || methInfo.className.startsWith("spartan/test");
      };

      final Consumer<Annotation> handleAnnotation = annotation -> {
        final String annotationType = annotation.getTypeName();
        logF(()->format("\tannotation: %s:%n", annotationType));
        CmdInfo cmdInfo = null;
        if (annotationType.compareTo(SupervisorMain.class.getName()) == 0) {
          if (isNullOrSpartanTestMethod.test(CommandDispatchInfo.this.spartanMainEntryPoint)) {
            logLn(()->"\t*** setting SupervisorMain program entry point ***");
            if (!isNullOrSpartanTestMethod.test(CommandDispatchInfo.this.spartanMainEntryPoint = new MethInfo(className,
                method.getName(), method.getDescriptor())))
            {
              isNonSpartanTestSupervisorMain.set(true);
            }
          }
        } else if (annotationType.compareTo(SupervisorCommand.class.getName()) == 0) {
          logLn(()->"\t*** setting SupervisorCommand dispatch entry ***");
          spartanSupervisorCmds.add(cmdInfo = new CmdInfo(className, method.getName(), method.getDescriptor()));
          if (!isNullOrSpartanTestMethod.test(cmdInfo)) {
            isNonSpartanTestSupervisorCmd.set(true);
          }
        } else if (annotationType.compareTo(ChildWorkerCommand.class.getName()) == 0) {
          logLn(()->"\t*** setting ChildWorkerCommand dispatch entry ***");
          final ChildCmdInfo childCmdInfo = new ChildCmdInfo(className, method.getName(), method.getDescriptor());
          cmdInfo = childCmdInfo;
          spartanChildWorkerCmds.add(childCmdInfo);
          if (!isNullOrSpartanTestMethod.test(cmdInfo)) {
            isNonSpartanTestChildWorkerCmd.set(true);
          }
        }
        @SuppressWarnings("unchecked")
        final java.util.Set<String> names = annotation.getMemberNames();
        if (names != null) {
          final CmdInfo cmdInfoArg = cmdInfo;
          assert(cmdInfoArg != null);
          names.stream()
            .filter(Objects::nonNull)
            .filter(spartanAnnotationValidMetaData::contains)
            .forEach(e -> handleAnnotationValue(annotation, e, cmdInfoArg));
        }
      };
  
      logF(()->format("method: %s.%s%s%n", className, method.getName(), method.getDescriptor()));
  
      Arrays.stream(annotations)
        .filter(Objects::nonNull)
        .filter(annotation -> spartanAnnotations.contains(annotation.getTypeName()))
        .forEach(handleAnnotation);

      if (isNonSpartanTestSupervisorMain.get() || isNonSpartanTestSupervisorCmd.get()) {
        spartanSupervisorCmds.removeIf(isNullOrSpartanTestMethod);
      }

      if (isNonSpartanTestSupervisorMain.get() || isNonSpartanTestChildWorkerCmd.get()) {
        spartanChildWorkerCmds.removeIf(isNullOrSpartanTestMethod);
      }
    }
  }
}
