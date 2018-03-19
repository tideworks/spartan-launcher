/* ByteArrayOutputStream.java

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
package spartan.util;

public class ByteArrayOutputStream extends java.io.ByteArrayOutputStream {
  public ByteArrayOutputStream(int initAllocSize) { super(initAllocSize); }
  public ByteArrayOutputStream(byte[] buf) { super(); super.buf = buf; }
  public byte[] getBuffer() { return super.buf; }
}
