/* spartan-exception.h

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
#ifndef __SPARTAN_EXCEPTION_H__
#define __SPARTAN_EXCEPTION_H__

#include <exception>
#include <memory>
#include <string>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreorder"
class spartan_exception : public std::exception {
protected:
  virtual void make_abstract() = 0;
protected:
  static void free_nm(char *p);
  std::unique_ptr<char, decltype(&free_nm)> _nm{ nullptr, &free_nm };
  std::string _msg;
  char* type_name(const char * const mangled_name);
  explicit spartan_exception(const char * const mangled_type_name, const char * const msg)
    : _msg{ msg } { _nm.reset(type_name(mangled_type_name)); }
  explicit spartan_exception(const char * const mangled_type_name, std::string &msg)
    : _msg{ std::move(msg) } { _nm.reset(type_name(mangled_type_name)); }
  spartan_exception() = default;
public:
  spartan_exception(const char * const msg) = delete;
  spartan_exception(std::string &&) = delete;
  spartan_exception(const std::string &) = delete;
  spartan_exception(std::string &) = delete;
  spartan_exception(const spartan_exception &) = delete;
  spartan_exception& operator=(const spartan_exception &) = delete;
  spartan_exception(spartan_exception &&) = delete;
  spartan_exception& operator=(spartan_exception &&) = delete;
  ~spartan_exception() override = default;
public:
  virtual const char* name() const throw()  { return _nm.get(); }
  const char* what() const throw() override { return _msg.c_str(); }
};
#pragma GCC diagnostic pop

#define DECL_EXCEPTION(x) \
class x##_exception : public spartan_exception {\
protected:\
  void make_abstract() override {}\
public:\
  x##_exception() = delete;\
  explicit x##_exception(const char * const msg) : spartan_exception{ typeid(*this).name(), msg } {}\
  explicit x##_exception(std::string &&msg) : spartan_exception{ typeid(*this).name(), msg } {}\
  x##_exception(const std::string &) = delete;\
  x##_exception(std::string &) = delete;\
  x##_exception(const x##_exception &) = delete;\
  x##_exception& operator=(const x##_exception &) = delete;\
  x##_exception(x##_exception &&ex) noexcept : spartan_exception() { this->operator=(std::move(ex)); }\
  x##_exception& operator=(x##_exception &&ex) noexcept {\
    this->_nm  = std::move(ex._nm);\
    this->_msg = std::move(ex._msg);\
    return *this;\
  }\
  ~x##_exception() override = default;\
};

std::string get_unmangled_name(const char * const mangled_name);

#endif // __SPARTAN_EXCEPTION_H__