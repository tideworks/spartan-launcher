/* FifoPipeOutputStream.java

Copyright 2015 - 2016 Tideworks Technology
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
import java.io.IOException;

public class FifoPipeOutputStream extends java.io.FileOutputStream {
  static {
    System.loadLibrary("spartan-shared");
  }
  protected final String pipeName;
  public static native void unlinkPipeName(final String pipeName);
  public FifoPipeOutputStream(java.io.FileDescriptor fdObj, String name) {
    super(fdObj);
    pipeName = name;
  }
  @Override
  public void close() throws IOException {
    try {
      super.close();
    }
    finally {
      unlinkPipeName(pipeName);
    }
  }
}

/*
public class FifoPipeOutputStream extends java.io.OutputStream {
  protected static final String errMsgFmt = "FIFO named pipe \"%s\" error - %s";
  static {
    System.loadLibrary("spartan");
  }
  protected final int fd;
  protected final String pipeName;
  // native methods returning String return null on success, otherwise is error text
  protected native String writePByte(int fd, int b);
  protected native String writePBlock(int fd, byte[] b, int off, int len);
  protected native String flushP(int fd);
  protected native String closeP(int fd);
  protected native void unlinkPipeName(final String pipeName);
  public FifoPipeOutputStream(int fd, String pipeName) {
    super();
    this.fd = fd;
    this.pipeName = pipeName;
  }
  @Override
  public void write(int b) throws IOException {
    final String rslt = writePByte(fd, b);
    if (rslt != null) {
      throw new IOException(String.format(errMsgFmt, pipeName, rslt));
    }
  }
  @Override
  public void write(byte[] b) throws IOException {
    final String rslt = writePBlock(fd, b, 0, b.length);
    if (rslt != null) {
      throw new IOException(String.format(errMsgFmt, pipeName, rslt));
    }
  }
  @Override
  public void write(byte[] b, int off, int len) throws IOException {
    final String rslt = writePBlock(fd, b, off, len);
    if (rslt != null) {
      throw new IOException(String.format(errMsgFmt, pipeName, rslt));
    }
  }
  @Override
  public void flush() throws IOException {
    final String rslt = flushP(fd);
    if (rslt != null) {
      throw new IOException(String.format(errMsgFmt, pipeName, rslt));
    }
  }
  @Override
  public void close() throws IOException {
    try {
      final String rslt = closeP(fd);
      if (rslt != null) {
        throw new IOException(String.format(errMsgFmt, pipeName, rslt));
      }
    }
    finally {
      unlinkPipeName(pipeName);
    }
  }
}
*/

