/* path-concat.cpp

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
#include <string>
#include "path-concat.h"

SO_EXPORT std::string path_concat(const char * const str1, const char * const str2) {
  std::string path_rslt{str1};
  const auto len = path_rslt.length();
  if (len > 0) {
    auto &last_ch = path_rslt[len - 1];
    if (last_ch != '/' && last_ch != '\\') {
      path_rslt += kPathSeparator;
    } else if (last_ch != kPathSeparator) {
      last_ch = kPathSeparator;
    }
  }
  return path_rslt += str2;
}