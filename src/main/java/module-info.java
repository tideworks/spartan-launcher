module spartan.launcher {
  requires java.base;
  requires javassist;
  requires static logback.core;
  requires static logback.classic;
  opens spartan.launcher to spartan.launcher;
  exports spartan_startup;
  exports spartan;
  exports spartan.annotations;
  exports spartan.fstreams;
  exports spartan.util;
  exports spartan.util.io;
}
