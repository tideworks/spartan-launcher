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
#include <functional>
#include <string>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreorder"
class spartan_exception: public std::exception
{
  protected:
    std::unique_ptr<char,std::function<void(char*)>> _nm;
    const std::string _msg;
    char* type_name(const char * const mangled_name);
    static void free_nm(char *p);
    spartan_exception(const char * const mangled_type_name, const char * const msg)
      : _nm(type_name(mangled_type_name), free_nm), _msg(msg) {}
    spartan_exception(const char * const mangled_type_name, std::string &msg)
      : _nm(type_name(mangled_type_name), free_nm), _msg(std::move(msg)) {}
  public:
    spartan_exception(const char * const msg) : spartan_exception(typeid(*this).name(), msg) {}
    spartan_exception(std::string&& msg) : spartan_exception(typeid(*this).name(), msg) {}
    spartan_exception(const spartan_exception&) = delete;
    spartan_exception(spartan_exception&& a) noexcept : _nm(a._nm.release()), _msg(std::move(a._msg)) {}
    virtual const char* name() const throw() { return _nm.get(); }
    virtual const char* what() const throw() { return _msg.c_str(); }
};
#pragma GCC diagnostic pop

#define DECL_EXCEPTION(x) \
class x##_exception: public spartan_exception {\
  public:\
    x##_exception(const char * const msg) : spartan_exception(typeid(*this).name(), msg) {}\
    x##_exception(std::string&& msg) : spartan_exception(typeid(*this).name(), msg) {}\
    x##_exception(const x##_exception&) = delete;\
    x##_exception(x##_exception&&) = default;\
};

std::string get_unmangled_name(const char * const mangled_name);

#endif // __SPARTAN_EXCEPTION_H__

