/* shm.cpp

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
#include <string>
#include <cstring>
#include <memory>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "format2str.h"
#include "log.h"
#include "shm.h"

//#undef NDEBUG // uncomment this line to enable asserts in use below
#include <cassert>

using namespace logger;

extern const char * progname();

namespace shm {

  static std::string get_shm_name() {
    return std::string("/") + progname();
  }

  static int open_shm(const std::string& shm_name, int oflag, mode_t mode) {
    auto fd = shm_open(shm_name.c_str(), oflag, mode);
    if (fd == -1) {
      throw shared_mem_exception(format2str("failed shm_open(\"%s\"):\n\t%s", shm_name.c_str(), strerror(errno)));
    }
    return fd;
  }

  struct fd_t {
  private:
    const int _fd;
  public:
    explicit fd_t(int fd) noexcept : _fd(fd) {}
    int fd() const noexcept { return _fd; }
  };

  auto const cleanup_fd = [](fd_t* p) {
    if (p != nullptr) {
      close(p->fd());
    }
  };

  std::tuple<void*,size_t > allocate(int pages) {
    assert(pages > 0);
    const std::string shm_name = get_shm_name();
    fd_t fd_wrpr{ open_shm(shm_name, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR) };
    std::unique_ptr<fd_t, decltype(cleanup_fd)> spfd(&fd_wrpr, cleanup_fd);
    const auto pg_size = sysconf(_SC_PAGE_SIZE);
    const auto length = static_cast<size_t >(pages * pg_size);
    if (ftruncate(spfd->fd(), length) == -1) {
      throw shared_mem_exception(format2str("failed ftruncate(%lu) on \"%s\" shared memory object:\n\t%s",
                                            length, shm_name.c_str(), strerror(errno)));
    }
    void * const rtn = mmap(nullptr, length, PROT_READ|PROT_WRITE, MAP_SHARED, spfd->fd(), 0);
    if (rtn == MAP_FAILED) {
      throw shared_mem_exception(format2str("failed mmap(%lu) on \"%s\" shared memory object:\n\t%s",
                                            length, shm_name.c_str(), strerror(errno)));
    }
    return std::make_tuple(rtn, length);
  }

  std::tuple<void*,size_t> read_access() {
    const std::string shm_name = get_shm_name();
    fd_t fd_wrpr{ open_shm(shm_name, O_RDONLY, S_IRUSR) };
    std::unique_ptr<fd_t, decltype(cleanup_fd)> spfd(&fd_wrpr, cleanup_fd);
    struct stat buf{};
    if (fstat(spfd->fd(), &buf) == -1) {
      throw shared_mem_exception(format2str("failed fstat() on \"%s\" shared memory object:\n\t%s",
                                            shm_name.c_str(), strerror(errno)));
    }
    const auto length = static_cast<size_t >(buf.st_size);
    void * const rtn = mmap(nullptr, length, PROT_READ, MAP_SHARED, spfd->fd(), 0);
    if (rtn == MAP_FAILED) {
      throw shared_mem_exception(format2str("failed mmap(%lu) on \"%s\" shared memory object:\n\t%s",
                                            length, shm_name.c_str(), strerror(errno)));
    }
    return std::make_tuple(rtn, length);
  }

  void unmap(void *addr, size_t length) {
    const std::string shm_name = get_shm_name();
    if (munmap(addr, length) == -1) {
      throw shared_mem_exception(format2str("failed munmap(0x%p, %lu) on \"%s\" shared memory object:\n\t%s",
                                            addr, length, shm_name.c_str(), strerror(errno)));
    }
  }

  void unlink() noexcept {
    const std::string shm_name = get_shm_name();
    if (shm_unlink(shm_name.c_str()) == -1) {
      log(LL::WARN, "failed shm_unlink(\\\"%s\\\"):\n\t%s", shm_name.c_str(), strerror(errno));
    }
  }

  ShmAllocator::~ShmAllocator() {
    unmap(base_addr, max_size);
    unlink();
  }

  void* ShmAllocator::alloc(size_t size) {
    const auto new_offset = curr_offset + size;
    if (new_offset > max_size) {
      const std::string shm_name = get_shm_name();
      throw shared_mem_exception(format2str("shared memory space of \"%s\" exceeded by %ul bytes",
                                            shm_name.c_str(), new_offset - max_size));
    }
    void* const rtn = reinterpret_cast<char*>(base_addr) + curr_offset;
    curr_offset = static_cast<int>(new_offset);
    return rtn;
  }

  void ShmAllocator::commit(size_t len) {
    if (msync(base_addr, len, MS_SYNC) == -1) {
      const std::string shm_name = get_shm_name();
      throw shared_mem_exception(format2str("failed msync(0x%p, %lu) on \"%s\" shared memory object:\n\t%s",
                                            base_addr, len, shm_name.c_str(), strerror(errno)));
    }
  }

  ShmAllocator* make(int pages) {
    const auto rslt = allocate(pages);
    return ::new ShmAllocator(std::get<0>(rslt), std::get<1>(rslt));
  }
} // namespace shm

void* operator new(std::size_t size, shm::ShmAllocator& shm_alloc) { return shm_alloc.alloc(size); }
void* operator new[] (std::size_t size, shm::ShmAllocator& shm_alloc) { return shm_alloc.alloc(size); }

// This declaration appears in assert.h and is part of stdc library definition. However,
// it is dependent upon conditional compilation controlled via NDEBUG macro definition.
// Here we are using it regardless of whether is debug or non-debug build, so declaring
// it extern explicitly.
extern "C" void __assert (const char *__assertion, const char *__file, int __line)
     __THROW __attribute__ ((__noreturn__));

void operator delete(void*/*unused*/, shm::ShmAllocator &/*unused*/) {
  __assert("do not use placement delete operator() on shared memory allocations - regard them as permanent",
           __FILE__, __LINE__);
}

void operator delete[] (void*/*unused*/, shm::ShmAllocator &/*unused*/) {
  __assert("do not use placement delete[] operator() on shared memory allocations - regard them as permanent",
           __FILE__, __LINE__);
}
