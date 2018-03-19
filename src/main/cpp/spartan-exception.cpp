/* spartan-exception.cpp

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
#include <cstdlib>
#include <cxxabi.h>
#include "log.h"
#include "spartan-exception.h"

using logger::log;
using logger::LL;

char* spartan_exception::type_name(const char * const mangled_name) {
  int status;
  return abi::__cxa_demangle(mangled_name, 0, 0, &status);
}

void spartan_exception::free_nm(char *p) {
#if defined(TEST_SPARTAN_EXCEPTION)
  printf("DEBUG - freeing string: \"%s\"\n", p);
#endif
  std::free(p);
}

std::string get_unmangled_name(const char * const mangled_name) {
  auto const free_nm = [](char *p) { std::free(p); };
  int status;
  auto const pnm = abi::__cxa_demangle(mangled_name, 0, 0, &status);
  std::unique_ptr<char,decltype(free_nm)> nm_sp(const_cast<char*>(pnm), free_nm);
  return std::string(nm_sp.get());
}

#if defined(TEST_SPARTAN_EXCEPTION)

DECL_EXCEPTION(create_jvm)

int main(int argc, char **argv) {
  {
    const create_jvm_exception e("my error first exception");
    printf("%s: INFO - %s: %s\n", argv[0], e.name(), e.what());
  }

  try {
    throw create_jvm_exception("my error second exception");
  } catch(const create_jvm_exception& e) {
    printf("%s: INFO - %s: %s\n", argv[0], e.name(), e.what());
  }

  printf("%s: INFO - exiting program\n", argv[0]);
  return 0;
}
#endif
