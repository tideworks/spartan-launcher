/* memBufStream.h

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
#ifndef MEMBUF_STREAM_MEMBUFSTREAM_H
#define MEMBUF_STREAM_MEMBUFSTREAM_H

#include <streambuf>
#include <tuple>

class memBufStream : public std::streambuf {
private:
  const bool forInputOnly;
protected:
  explicit memBufStream(void* buf, size_t size, bool forInput) : forInputOnly(forInput) {
    setp(reinterpret_cast<char*>(buf), reinterpret_cast<char*>(buf) + size);
    if (forInput) {
      setg(pbase(), pbase(), epptr());
    }
  }
private:
  memBufStream() = delete;
  memBufStream(const memBufStream &) = delete;
  memBufStream &operator= (const memBufStream &) = delete;
public:
  int_type overflow(int_type /*ch*/) override;
  std::tuple<char*, size_t> getMemBuf() const {
    return std::make_tuple(pbase(), forInputOnly ? epptr() - pbase() : pptr() - pbase());
  };
  void resetForInput() { setg(pbase(), pbase(), forInputOnly ? epptr() : pptr()); }
};

class memBufOStream : public memBufStream {
public:
  explicit memBufOStream(void* buf, size_t size) : memBufStream(buf, size, false) {}
private:
  memBufOStream() = delete;
  memBufOStream(const memBufOStream &) = delete;
  memBufOStream &operator= (const memBufOStream &) = delete;
};

class memBufIStream : public memBufStream {
public:
  explicit memBufIStream(void* buf, size_t size) : memBufStream(buf, size, true) {}
private:
  memBufIStream() = delete;
  memBufIStream(const memBufIStream &) = delete;
  memBufIStream &operator= (const memBufIStream &) = delete;
public:
  std::tuple<char*, size_t> getUtilizedMemBuf() const {
    return std::make_tuple(pbase(), gptr() - pbase());
  };
};

#endif //MEMBUF_STREAM_MEMBUFSTREAM_H
