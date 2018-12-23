/* str-split.cpp

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
#include "str-split.h"

//#undef NDEBUG // uncomment this line to enable asserts in use below
#include <cassert>

// Utility function that can be used to split an input C string into a returned vector
// of std::string parts where is split on a specified character. By default will split
// on every occurrence of the split character, but if limit argument is explicitly
// passed, then will split on (limit - 1) occurrences; e.g., a limit value of 1 returns
// a vector containing a std::string that is a full copy of the original input C string.
std::vector<std::string> str_split(const char *s, const char c, int limit) {
  const size_t BUF_SIZE = 64;
  std::string buf; // string fragment buffer
  buf.reserve(BUF_SIZE);
  std::vector<std::string> v;
  assert(limit >= 0); // negative limit value is bad input
  if (limit < 0) {
    limit = 0; // will force bad input to behave as default condition
  }
  v.reserve(limit == 0 ? 2 : limit); // as default, anticipate at least 2 string fragments to be returned

  // appends a string fragment to the vector (that is to be returned as result)
  auto const append_str_to_vec = [&buf, &v]() {
    buf.shrink_to_fit();
    v.push_back(std::move(buf));
    buf.clear();
    buf.reserve(BUF_SIZE);
  };

  // scan the input string looking for split delimiter character and doing split
  int added = 0;
  bool at_limit = false;
  char n;
  while((n = *s++) != 0) {
    if (n != c) {
      buf += n; // accumulate character into string fragment buffer
    } else if (buf.size() > 0) {
      if (limit != 0 && ++added >= limit) { // when limit is zero, will return as many fragments as found
        at_limit = true;
        buf += n;
      }
      if (!at_limit) {
        append_str_to_vec(); // append current string fragment to result vector
      }
    }
  }
  if (buf.size() > 0) {
    // append last string fragment to result vector
    buf.shrink_to_fit();
    v.push_back(std::move(buf));
  }

  v.shrink_to_fit(); // result vector and its string elements are now optimally sized for storage

  return v; // rely on return value optimization
}
