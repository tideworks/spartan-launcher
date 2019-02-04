module spartan.launcher {
  requires java.base;
  requires javassist;
  requires static logback.core;
  requires static logback.classic;
  exports spartan;
  exports spartan.annotations;
  exports spartan.fstreams;
  exports spartan.util;
  exports spartan.util.io;
}