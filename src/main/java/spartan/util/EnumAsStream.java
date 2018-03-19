/* EnumAsStream.java

Copyright 2016 Tideworks Technology
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

import java.util.Enumeration;
import java.util.Spliterator;
import java.util.Spliterators;
import java.util.function.Consumer;
import java.util.stream.Stream;
import java.util.stream.StreamSupport;

public final class EnumAsStream {
  public static <T> Stream<T> enumerationAsStream(Enumeration<T> e) {
    return StreamSupport.stream(new Spliterators.AbstractSpliterator<T>(Long.MAX_VALUE, Spliterator.ORDERED) {
      public boolean tryAdvance(Consumer<? super T> action) {
        if (e.hasMoreElements()) {
          action.accept(e.nextElement());
          return true;
        }
        return false;
      }
      public void forEachRemaining(Consumer<? super T> action) {
        while (e.hasMoreElements())
          action.accept(e.nextElement());
      }
    }, false);
  }
}