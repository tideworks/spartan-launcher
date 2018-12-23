/* format2str.cpp

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
#include <cstdio>
#include "format2str.h"

//#undef NDEBUG // uncomment this line to enable asserts in use below
#include <cassert>

std::string vformat2str(const char *const fmt, va_list ap) {
  int strbuf_size = 256;
  int n = strbuf_size;
  char *strbuf = (char*) alloca(strbuf_size);
  va_list parm_copy;
  va_copy(parm_copy, ap);
  {
    n = vsnprintf(strbuf, (size_t) n, fmt, ap);
    assert(n > 0);
    if (n >= strbuf_size) {
      strbuf = (char*) alloca(strbuf_size = ++n);
      n = vsnprintf(strbuf, (size_t) n, fmt, parm_copy);
      assert(n > 0 && n < strbuf_size);
    }
  }
  va_end(parm_copy);
  return std::string(strbuf);
}

std::string format2str(const char *const fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  auto rslt( vformat2str(fmt, ap) );
  va_end(ap);
  return rslt;
}
