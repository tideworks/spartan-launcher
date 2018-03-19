/* streambuf-wrapper.h

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
#ifndef __STREAMBUF_WRAPPER_H__
#define __STREAMBUF_WRAPPER_H__

#include <streambuf>

// Custom wrapper class derived from std::streambuf that encapsulates
// a pre-existing char buffer of a specified length. Intended for use
// with std::istream to where buffer is accessed in read-only manner.
class streambufWrapper : public std::streambuf {
protected:
  inline streambufWrapper() = default;
public:
  inline streambufWrapper(const char*pbuf, size_t size) : streambufWrapper() {
    setg(const_cast<char*>(pbuf), const_cast<char*>(pbuf), const_cast<char*>(pbuf) + size);
  }
};

#endif //__STREAMBUF_WRAPPER_H__