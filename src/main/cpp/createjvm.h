/* createjvm.h

Copyright 2015 - 2017 Tideworks Technology
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
#ifndef __CREATEJVM_H__
#define __CREATEJVM_H__

#include "spartan-exception.h"
#include <tuple>
#include <dlfcn.h>
#include <jni.h>

// declare create_jvm_exception
DECL_EXCEPTION(create_jvm)

typedef std::tuple<JavaVM*,JNIEnv*> jvm_create_t;

std::string determine_jvmlib_path();
void* open_jvm_runtime_module(const char * const jvmlib_path);
jvm_create_t create_jvm(void * const hlibjvm, const char *jvm_override_optns);

#endif // __CREATEJVM_H__
