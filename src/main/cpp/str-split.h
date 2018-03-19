/* str-split.h

Copyright 2017 Tideworks Technology
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
#ifndef __STR_SPLIT_H__
#define __STR_SPLIT_H__

#include <string>
#include <vector>
#include "so-export.h"

extern "C" {

// Utility function that can be used to split an input C string into a returned vector
// of std::string parts where is split on a specified character. By default will split
// on every occurrence of the split character, but if limit argument is explicitly
// passed, then will split on (limit - 1) occurrences; e.g., a limit value of 1 returns
// a vector containing a std::string that is a full copy of the original input C string.
SO_EXPORT std::vector<std::string> str_split(const char *s, const char c, int limit = 0);

}

#endif //__STR_SPLIT_H__