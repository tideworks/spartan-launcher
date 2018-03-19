/* shm.h

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
#ifndef SPARTAN_SHM_H
#define SPARTAN_SHM_H

#include "spartan-exception.h"

namespace shm {

  // declare shared_mem_exception
  DECL_EXCEPTION(shared_mem)

  // shared memory operations
  std::tuple<void*,size_t> allocate(int pages); // caller originates the shared memory object (supervisor process)
  std::tuple<void*,size_t> read_access();       // client read-only access of shared memory object (other processes)
  void unmap(void* addr, size_t length);        // all use to unmap their respective access of the shared memory area
  void unlink() noexcept;                       // original owner uses to remove shared memory object name

  class ShmAllocator : public std::nothrow_t {
  protected:
    void* const base_addr;
    const size_t max_size;
    int curr_offset;
    ShmAllocator(void* base_addr, size_t max_size) noexcept: base_addr(base_addr), max_size(max_size), curr_offset(0) {}
  public:
    ~ShmAllocator();
    void* alloc(size_t size);
    void commit(size_t);
    void commit() { commit(curr_offset); }
    std::tuple<char*, size_t> getMemBuf() const noexcept {
      return std::make_tuple(reinterpret_cast<char*>(base_addr), max_size);
    }
    std::tuple<char*, size_t> getUtilizedMemBuf() const noexcept {
      return std::make_tuple(reinterpret_cast<char*>(base_addr), curr_offset);
    }
    std::tuple<char*, size_t> getRemainingMemBuf() const noexcept {
      return std::make_tuple(reinterpret_cast<char*>(base_addr) + curr_offset, max_size - curr_offset);
    }
  public:
    friend ShmAllocator* make(int pages); // must use this to instantiate ShmAllocator
  };

  ShmAllocator* make(int pages);
}

void* operator new(std::size_t size, shm::ShmAllocator& shm_alloc);
void* operator new[] (std::size_t size, shm::ShmAllocator& shm_alloc);
void operator delete(void*, shm::ShmAllocator &) __attribute__ ((deprecated));
void operator delete[] (void*, shm::ShmAllocator &) __attribute__ ((deprecated));

#endif //SPARTAN_SHM_H
