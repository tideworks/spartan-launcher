/* memBufStream.cpp

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
#include "memBufStream.h"

// This declaration appears in assert.h and is part of stdc library definition. However,
// it is dependent upon conditional compilation controlled via NDEBUG macro definition.
// Here we are using it regardless of whether is debug or non-debug build, so declaring
// it extern explicitly.
extern "C" void __assert (const char *__assertion, const char *__file, int __line)
__THROW __attribute__ ((__noreturn__));


memBufStream::int_type memBufStream::overflow(int_type /*ch*/) {
  __assert("memBufStream::overflow() - memory buffer exhausted", __FILE__, __LINE__);
  //throw std::ios_base::failure("memory buffer exhausted");
}
