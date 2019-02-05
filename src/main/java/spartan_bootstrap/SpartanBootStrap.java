package spartan_bootstrap;

import java.io.*;
import java.net.URI;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.*;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.function.Predicate;
import java.util.jar.Attributes;
import java.util.jar.JarInputStream;
import java.util.jar.Manifest;
import java.util.stream.Collectors;

import java.lang.module.Configuration;
import java.lang.module.ModuleDescriptor;
import java.lang.module.ModuleFinder;
import java.lang.module.ModuleReader;
import java.lang.module.ModuleReference;

@SuppressWarnings("unused")
final class SpartanBootStrap {
  private static final String clsName = SpartanBootStrap.class.getSimpleName();
  private static final char PSC = ':' != File.pathSeparatorChar ? ':' : ';';

  @SuppressWarnings("WeakerAccess")
  private static final class Pair<T, U> implements Serializable {
    private final T left;
    private final U right;
    public Pair(T t, U u) { left = t; right = u; }
    public Pair() { this(null, null); }
    public T getLeft()  { return left;  }
    public U getRight() { return right; }
  }

  @SuppressWarnings("unchecked")
  private static Pair<ModuleLayer, byte[]> supervisorJvmBootStrap(String[] jvmArgs, boolean isDebug) throws Exception {
    if (isDebug) {
      final String output = String.join("\" \"", jvmArgs);
      System.err.printf("DEBUG: >> %s.supervisorJvmBootStrap(\"%s\")%n", clsName, output);
    }

    final Consumer<String> noArgumentSuppliedErr =
          optn -> System.err.printf("ERROR: no argument supplied for option %s%n", optn);

    String[] modPaths = new String[0];
    String modWithEntryMain = "";
    Path spartan_launcher_path = null;

    final int argsLen = jvmArgs.length;
    if (argsLen > 1) {
      for (int i = 0; i < argsLen; i++) {
        final String arg = jvmArgs[i];
        final String[] argParts = arg.split("=", 2);
        final String argOptn = argParts[0].toLowerCase();
        String argVal = argParts.length > 1 ? argParts[1] : null;
        switch(argOptn) {
          case "--module-path":
          case "-p": {
            String modPath;
            if (argVal != null) {
              modPath = argVal;
            } else {
              final int n = i + 1;
              if (n < argsLen) {
                i++;
                modPath = jvmArgs[n];
              } else {
                noArgumentSuppliedErr.accept(argOptn);
                continue;
              }
            }
            modPaths = modPath.replace(PSC, File.pathSeparatorChar).split(File.pathSeparator);
            break;
          }
          case "--module":
          case "-m": {
            if (argVal != null) {
              modWithEntryMain = argVal;
            } else {
              final int n = i + 1;
              if (n < argsLen) {
                i++;
                modWithEntryMain = jvmArgs[n];
              } else {
                noArgumentSuppliedErr.accept(argOptn);
              }
            }
            System.err.printf("WARNING: %s %s%n\t%s ignored - use Spartan annotation to mark service entry method:%n",
                  argOptn, modWithEntryMain, argOptn);
            break;
          }
          default: {
            if (argOptn.startsWith("-xbootclasspath/a:")) { /* doing coerced lowercase comparison */
              final int offset = arg.indexOf(':') + 1;
              if (offset < arg.length()) {
                argVal = arg.substring(offset).replace(PSC, File.pathSeparatorChar).split(File.pathSeparator)[0];
                final Path argValPath = Paths.get(argVal);
                final String fileName = argValPath.getFileName().toString().toLowerCase();
                if (fileName.startsWith("spartan")) { /* doing coerced lowercase comparison */
                  spartan_launcher_path = argValPath;
                }
              }
            }
            break;
          }
        }
      }
    }

    if (modPaths.length > 0) {
      final ArrayList<Path> validatedModuleFilePathsList = new ArrayList<>(modPaths.length + 1);
      if (spartan_launcher_path != null) {
        validatedModuleFilePathsList.add(spartan_launcher_path);
      }
      for (final String modPath : modPaths) {
        Path modFilePath = Paths.get(new File(modPath).toURI());
        final Path normPath = modFilePath.normalize();
        if (Files.exists(normPath)) {
          modFilePath = normPath;
        } else if (Files.notExists(modFilePath)) {
          System.err.printf("WARNING: module path does not exist in file system - skipping:%n\t\"%s\"%n", modPath);
          continue;
        }
        validatedModuleFilePathsList.add(modFilePath);
      }
      for (final Path modPath : validatedModuleFilePathsList.toArray(new Path[0])) {
        if (modPath.toString().endsWith(".jar") && Files.isRegularFile(modPath)) {
          checkManifestClassPathDependencies(validatedModuleFilePathsList, modPath);
        }
      }
      final Set<Path> validatedModuleFilePathsSet = new LinkedHashSet<>(validatedModuleFilePathsList);
      final Path[] validatedModuleFilePathsArray = validatedModuleFilePathsSet.toArray(new Path[0]);
      return (Pair<ModuleLayer, byte[]>) resolveModuleLayer(validatedModuleFilePathsArray, true, isDebug);
    }

    return new Pair<>();
  }

  @SuppressWarnings("unchecked")
  private static ModuleLayer childWorkerJvmBootStrap(byte[] serializedModulePaths, boolean isDebug) throws Exception {
    if (isDebug) {
      System.err.printf(">> %s.childWorkerJvmBootStrap(..)%n", clsName);
    }

    List<Pair<String, String>> modulesList = (List<Pair<String, String>>) Collections.EMPTY_LIST;

    if (serializedModulePaths != null) {
      final ByteArrayInputStream byteStream = new ByteArrayInputStream(serializedModulePaths);
      try (final ObjectInputStream inStrm = new ObjectInputStream(byteStream)) {
        modulesList = (List<Pair<String, String>>) inStrm.readObject();
      }
    }

    if (!modulesList.isEmpty()) {
      final String[] none = new String[0];
      final Function<String, Path> str2Path = str -> Paths.get(str, none);
      final Path[] moduleFilePathsArray = modulesList.stream().map(Pair::getRight).map(str2Path).toArray(Path[]::new);
      return (ModuleLayer) resolveModuleLayer(moduleFilePathsArray, false, isDebug);
    }

    return null;
  }

  private static Object resolveModuleLayer(Path[] moduleFilePathsArray, boolean doSerializeModList, boolean isDebug)
        throws Exception
  {
    final ModuleFinder moduleFinder = ModuleFinder.of(moduleFilePathsArray);
    final Set<ModuleReference> moduleRefs = moduleFinder.findAll();

    if (isDebug) {
      moduleRefs.forEach(SpartanBootStrap::printModuleInfo);
    }

    final ModuleLayer bootLayer = ModuleLayer.boot();
    final Configuration bootLayerConfig = bootLayer.configuration();
    final Set<String> moduleNamesSet = moduleRefs.stream()
                                      .map(ModuleReference::descriptor)
                                      .map(ModuleDescriptor::name)
                                      .collect(Collectors.toSet());
    final Configuration newConfig = bootLayerConfig.resolve(moduleFinder, ModuleFinder.of(), moduleNamesSet);
    final ClassLoader sysClassLoader = ClassLoader.getSystemClassLoader();
    final ModuleLayer newLayer = bootLayer.defineModulesWithOneLoader(newConfig, sysClassLoader);

    if (doSerializeModList) {
      final List<Pair<String, String>> modulesList = moduleRefs.stream()
                                                           .map(SpartanBootStrap::makeModuleEntry)
                                                           .filter(pair -> pair.getRight() != null)
                                                           .collect(Collectors.toList());
      final ByteArrayOutputStream byteStream = new ByteArrayOutputStream(1024 * 8);
      try (final ObjectOutputStream out = new ObjectOutputStream(byteStream)) {
        out.writeObject(modulesList);
      }
      return new Pair<>(newLayer, byteStream.toByteArray());
    }

    return newLayer;
  }

  private static Pair<String, String> makeModuleEntry(ModuleReference mr) {
    final String name = mr.descriptor().name();
    final Optional<URI> location = mr.location();
    final URI uri = location.orElse(null);
    final String path = uri != null ? Paths.get(uri).toString() : null;
    return new Pair<>(name, path);
  }

  private static void checkManifestClassPathDependencies(final ArrayList<Path> jarPaths, final Path jarFile)
        throws IOException
  {
    final File jarDir = jarFile.getParent().toFile();
    try (final JarInputStream jarInputStream = new JarInputStream(Files.newInputStream(jarFile))) {
      final Manifest manifest = jarInputStream.getManifest();
      if (manifest != null) {
        final Attributes mainAttributes = manifest.getMainAttributes();
        if (mainAttributes != null) {
          final String jarClassPath = mainAttributes.getValue("Class-Path");
          if (jarClassPath != null) {
            final String[] manifestClassPath = jarClassPath.split("\\s+");
            jarPaths.ensureCapacity(jarPaths.size() + manifestClassPath.length + 1);
            Arrays.stream(manifestClassPath)
                  .filter(Objects::nonNull)
                  .map(fileName -> Paths.get(new File(jarDir, fileName).toURI()))
                  .forEach(path -> {
                    final Path normPath = path.normalize();
                    if (Files.exists(normPath)) {
                      path = normPath;
                    } else if (Files.notExists(path)) {
                      System.err.printf(
                        "WARNING: transitive module path does not exist in file system - skipping:%n\t\"%s\"%n", path);
                      return;
                    }
                    jarPaths.add(path);
                  });
          }
        }
      }
    }
  }

  private static void printModuleInfo(ModuleReference mr) {
    final Predicate<String> allowIt = str -> str.endsWith(".class") || str.equals("META-INF/MANIFEST.MF");

    final ModuleDescriptor md = mr.descriptor();
    final Optional<URI> location = mr.location();
    final URI uri = location.orElse(null);
    final Path path = uri != null ? Paths.get(uri) : null;
    System.err.printf("%nDEBUG: Module: %s, Location: %s%n", md.name(), path);
    try (final ModuleReader reader = mr.open()) {
      reader.list()
            .filter(allowIt)
            .forEach(System.err::println);
    } catch (IOException ioe) {
      uncheckedExceptionThrow(ioe);
    }
    System.err.println();
  }

  @SuppressWarnings({"unchecked", "Unused", "UnusedReturnValue"})
  private static <T extends Exception, R> R uncheckedExceptionThrow(Exception t) throws T { throw (T) t; }
}