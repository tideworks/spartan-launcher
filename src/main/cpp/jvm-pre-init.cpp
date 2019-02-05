/* jvm-pre-init.cpp

Copyright 2019 Tideworks Technology
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
#include <jni.h>
#include <cstring>
#include <popt.h>
#include <unistd.h>
#include "process-cmd-dispatch-info.h"
#include "string-view.h"
#include "jvm-pre-init.h"
#include "log.h"

using namespace logger;
using namespace jvm_pre_init;
using namespace bpstd;

/**
 * The below class methods are only invoked as helper methods from the
 * spartan invoke_java_method(..) function.
 */

#define DECL_DEFER_SMART_PTR(type_name, cleanup_callback)\
  auto const cleanup_callback = [this](jobject p) {\
    if (p != nullptr) {\
      this->_env->DeleteLocalRef(p);\
    }\
  };\
  using type_name = std::unique_ptr<_jobject, decltype(cleanup_callback)>

const char* jvm_pre_init_ctx::split_method_name_from_class_name(const char* const stfbuf,
                                                                const char* const full_method_name)
{
  char * str = const_cast<char*>(strrchr(stfbuf, '/'));
  if (str == nullptr || *(str + 1) == '\0') throw std::string(full_method_name);
  *str = '\0';  // null terminate the class name string
  return ++str; // return the method name string
}

static methodDescriptor make_ModuleLayer_findLoader_descriptor() {
  return methodDescriptor(
      "java/lang/ModuleLayer/findLoader",
      "(Ljava/lang/String;)Ljava/lang/ClassLoader;",
      true, WM::NONE);
}

methodDescriptor jvm_pre_init_ctx::make_obtainSerializedAnnotationInfo_descriptor() {
  return methodDescriptor(
      "spartan_startup/CommandDispatchInfo/obtainSerializedSysPropertiesAndAnnotationInfo",
      "()[B",
      true, WM::GET_CMD_DISPATCH_INFO);
}

static methodDescriptor make_supervisorJvmBootStrap_descriptor() {
  return methodDescriptor(
      "spartan_bootstrap/SpartanBootStrap/supervisorJvmBootStrap",
      "([Ljava/lang/String;Z)Lspartan_bootstrap/SpartanBootStrap$Pair;",
      true, WM::NONE);
}

static methodDescriptor make_childWorkerJvmBootStrap_descriptor() {
  return methodDescriptor(
      "spartan_bootstrap/SpartanBootStrap/childWorkerJvmBootStrap",
      "([BZ)Ljava/lang/ModuleLayer;",
      true, WM::NONE);
}

static methodDescriptor make_Pair_getLeft_descriptor() {
  return methodDescriptor(
      "spartan_bootstrap/SpartanBootStrap$Pair/getLeft",
      "()Ljava/lang/Object;",
      true, WM::NONE);
}

static methodDescriptor make_System_bootLayer_descriptor() {
  return methodDescriptor(
      "java/lang/System/bootLayer",
      "Ljava/lang/ModuleLayer;",
      true, WM::NONE);
}

/**
 * A helper method that is called by invoke_java_method(..) to set the system class loader
 * as the current thread class loader. Any by-int-value exceptions thrown are handled in
 * the function invoke_java_method(..).
 */
void jvm_pre_init_ctx::set_thread_class_loader_context() {
  DECL_DEFER_SMART_PTR(defer_jobj_t, defer_jobj);

  // save state of these local variables (may be referenced in caught exception handling code below)
  const char * const class_name_sav  = _class_name;
  const char * const method_name_sav = _method_name;

  _class_name = "java/lang/ClassLoader";
  jclass const clsLdr_cls = _env->FindClass(_class_name);
  if (clsLdr_cls == nullptr) throw 3;
  _method_name = "getSystemClassLoader";
  jmethodID const getSysClsLdr = _env->GetStaticMethodID(clsLdr_cls, _method_name, "()Ljava/lang/ClassLoader;");
  if (getSysClsLdr == nullptr) throw 4;

  // get system class loader
  defer_jobj_t cls_ldr_obj_sp(_env->CallStaticObjectMethod(clsLdr_cls, getSysClsLdr), defer_jobj);

  _class_name = "java/lang/Thread";
  jclass const thrd_cls = _env->FindClass(_class_name);
  if (thrd_cls == nullptr) throw 3;
  _method_name = "currentThread";
  jmethodID const currThrd = _env->GetStaticMethodID(thrd_cls, _method_name, "()Ljava/lang/Thread;");
  if (currThrd == nullptr) throw 4;

  // get current thread object
  defer_jobj_t curr_thrd_obj_sp(_env->CallStaticObjectMethod(thrd_cls, currThrd), defer_jobj);

  _method_name = "setContextClassLoader";
  jmethodID const set_cntx_cls_ldr = _env->GetMethodID(thrd_cls, _method_name, "(Ljava/lang/ClassLoader;)V");
  if (set_cntx_cls_ldr == nullptr) throw 4;

  // now set the system class loader on the current thread object as the thread's context class loader
  _env->CallVoidMethod(curr_thrd_obj_sp.get(), set_cntx_cls_ldr, cls_ldr_obj_sp.get());

  // restore state of these local variables
  _class_name  = class_name_sav;
  _method_name = method_name_sav;
}

/**
 * A helper method that is called to do a pre-initialization phase in the supervisor Java JVM
 * instance prior to invoking the Spartan service main(..) method. Any by-int-value exceptions
 * thrown are handled in the function invoke_java_method(..).
 *
 * @param method_descriptor Java method that is invoked to do pre-initialization - its class
 * will be locatable via the JVM -Xbootclasspath/a: classpath
 *
 * @param ss the Spartan runtime session state that is to be initialized by this helper method
 *
 * @return returns the shared memory allocator where Spartan runtime session state was persisted;
 * also returns a bool flag that will indicate if an Java exception has been thrown (the allocator
 * will be a nullptr if an exception was thrown)
 */
std::pair<shm::ShmAllocator *, bool> jvm_pre_init_ctx::pre_init_for_supervisor_jvm(
    const methodDescriptorBase &method_descriptor, sessionState &ss)
{
  DECL_DEFER_SMART_PTR(defer_jobj_t, defer_jobj);

  auto rtn = invoke_supervisor_jvm_bootstrap();
  defer_jobj_t module_layer_sp{ std::get<0>(rtn), defer_jobj };
  defer_jobj_t serialized_module_paths_sp{ std::get<1>(rtn), defer_jobj };
  bool was_exception_raised = std::get<2>(rtn);
  if (was_exception_raised) {
    return std::make_pair(nullptr, was_exception_raised);
  }

  jclass jcls = nullptr;

  if (module_layer_sp) {
    const methodDescriptor find_loader{ make_ModuleLayer_findLoader_descriptor() };
    const char *const module_layer_cls_name = strdupa(find_loader.c_str());
    const char *const findLoader_method = split_method_name_from_class_name(module_layer_cls_name, find_loader.c_str());

    // save state of these local variables (may be referenced in caught exception handling code below)
    const char * const class_name_sav  = _class_name;
    const char * const method_name_sav = _method_name;

    _class_name = module_layer_cls_name;
    jcls = _env->GetObjectClass(module_layer_sp.get());
    if (jcls == nullptr) throw 3;

    _method_name = findLoader_method;
    jmethodID const mid = _env->GetMethodID(jcls, findLoader_method, find_loader.desc_str());
    if (mid == nullptr) throw 4;

    // TODO: finish implementing using ModuleLayer.findLoader() to load class and execute targted method

    // restore state of these local variables
    _class_name  = class_name_sav;
    _method_name = method_name_sav;
  } else {
    jcls = _env->FindClass(_class_name);
    if (jcls == nullptr) throw 3;
  }

  jmethodID const get_cmd_dispatch_info = _env->GetStaticMethodID(jcls, _method_name, method_descriptor.desc_str());
  if (get_cmd_dispatch_info == nullptr) throw 4;

  log(LL::DEBUG, "%s() invoking static method \"%s\"", __FUNCTION__, method_descriptor.c_str());
  defer_jobj_t serialized_cmd_dispatch_info_sp{ _env->CallStaticObjectMethod(jcls, get_cmd_dispatch_info), defer_jobj };
  was_exception_raised = _env->ExceptionCheck() != JNI_FALSE;
  if (was_exception_raised) {
    return std::make_pair(nullptr, was_exception_raised);
  }

  auto const serialized_module_paths = reinterpret_cast<jbyteArray>(serialized_module_paths_sp.get());
  auto const serialized_cmd_dispatch_info = reinterpret_cast<jbyteArray>(serialized_cmd_dispatch_info_sp.get());
  auto pshm = cmd_dsp::CmdDispatchInfoProcessor{ _env, _class_name, _method_name, jcls, ss }
                            .process_initial_cmd_dispatch_info(serialized_module_paths, serialized_cmd_dispatch_info);

  return std::make_pair(pshm, was_exception_raised);
}

static jobjectArray get_empty_jargs(JNIEnv *env) {
  auto const jstr_cls = env->FindClass("java/lang/String");
  return env->NewObjectArray(0, jstr_cls, nullptr);
}

static jobjectArray jargs_from_args(JNIEnv *env, int argc, const char* argv[]) {
  auto const defer_jobj_array = [env](jobjectArray p) {
    if (p != nullptr) {
      env->DeleteLocalRef(p);
    }
  };
  using defer_jobj_array_t = std::unique_ptr<_jobjectArray, decltype(defer_jobj_array)>;

  auto const defer_jstr = [env](jstring p) {
    if (p != nullptr) {
      env->DeleteLocalRef(p);
    }
  };
  using defer_jstr_t = std::unique_ptr<_jstring, decltype(defer_jstr)>;

  if (argc > 0 && argv != nullptr) {
    // create Java array of strings corresponding to the input parameters argc and argv
    auto const jstr_cls  = env->FindClass("java/lang/String");
    auto const jargs_tmp = env->NewObjectArray(argc, jstr_cls, nullptr);
    if (jargs_tmp != nullptr) {
      defer_jobj_array_t jargs_array_sp{ jargs_tmp, defer_jobj_array };
      defer_jstr_t utf_str_sp{ nullptr, defer_jstr };
      for(int i = 0, j = 0; i < argc; i++) {
        const char* const arg = argv[i];
        log(LL::DEBUG, "argc index: %d, argv: %s\n", i, arg);
        utf_str_sp.reset(env->NewStringUTF(arg));
        env->SetObjectArrayElement(jargs_tmp, j++, utf_str_sp.get());
      }
      return jargs_array_sp.release();
    }
  }

  return get_empty_jargs(env);
}

static jobjectArray jargs_from_args(JNIEnv* const env) {
  const string_view jvm_cmd_line_args_sv{ jvm_cmd_line_args() };
  if (!jvm_cmd_line_args_sv.empty()) {
    int argc = 0;
    const char**argv = nullptr;

    int line_nbr = __LINE__ + 1;
    const auto rtn = poptParseArgvString(jvm_cmd_line_args_sv.c_str(), &argc, &argv);
    if (rtn != 0) {
      log(LL::FATAL, "%s(): %d: unexpected error - JVM options could not be parsed (program terminating): %s\n\t%s\n",
          __FUNCTION__, line_nbr, poptStrerror(rtn), jvm_cmd_line_args_sv.c_str());
      _exit(1);
    }

    // provide a RAII cleanup for the popt argv allocation
    std::unique_ptr<const char*, decltype(&std::free)> raii_argv_sp{ argv, &std::free };

    return jargs_from_args(env, argc, argv);
  }

  return get_empty_jargs(env);
}

std::tuple<jobject, jobject, bool> jvm_pre_init_ctx::invoke_supervisor_jvm_bootstrap() {
  DECL_DEFER_SMART_PTR(defer_jobj_t, defer_jobj);

  const methodDescriptor supervisorJvmBootStrap { make_supervisorJvmBootStrap_descriptor() };
  const char* const class_name = strdupa(supervisorJvmBootStrap.c_str());
  const char* const method_name = split_method_name_from_class_name(class_name, supervisorJvmBootStrap.c_str());

  // save state of these local variables (may be referenced in caught exception handling code below)
  const char * const class_name_sav  = _class_name;
  const char * const method_name_sav = _method_name;

  _class_name = class_name;
  jclass const cls = _env->FindClass(class_name);
  if (cls == nullptr) throw 3;

  _method_name = method_name;
  jmethodID const mid = _env->GetStaticMethodID(cls, method_name, supervisorJvmBootStrap.desc_str());
  if (mid == nullptr) throw 4;

  defer_jobj_t jvm_jargs_sp{ jargs_from_args(_env), defer_jobj };

  const auto is_debug = static_cast<jboolean>(get_level() <= LL::DEBUG ? JNI_TRUE : JNI_FALSE);

  jobject const pair = _env->CallStaticObjectMethod(cls, mid, jvm_jargs_sp.get(), is_debug);
  defer_jobj_t pair_sp{ pair, defer_jobj };
  bool was_exception_raised = _env->ExceptionCheck() != JNI_FALSE;

  defer_jobj_t module_layer_sp{ nullptr, defer_jobj };
  jobject serialized_module_paths = nullptr;

  if (!was_exception_raised) {
    const methodDescriptor pair_get_left{ make_Pair_getLeft_descriptor() };
    const char *const pair_class_name = strdupa(pair_get_left.c_str());
    const char *const get_left_method_name = split_method_name_from_class_name(pair_class_name, pair_get_left.c_str());

    _class_name = pair_class_name;
    jclass const pair_cls = _env->GetObjectClass(pair_sp.get());
    if (pair_cls == nullptr) throw 3;

    _method_name = get_left_method_name;
    jmethodID const pair_get_left_mid = _env->GetMethodID(pair_cls, get_left_method_name, pair_get_left.desc_str());
    if (pair_get_left_mid == nullptr) throw 4;

    jobject const module_layer = _env->CallObjectMethod(pair_sp.get(), pair_get_left_mid);
    module_layer_sp.reset(module_layer);
    was_exception_raised = _env->ExceptionCheck() != JNI_FALSE;

    if (!was_exception_raised && module_layer_sp) {
      was_exception_raised = set_system_boot_layer(module_layer_sp.get());

      if (!was_exception_raised) {
        const char *const get_right_method_name = "getRight";
        _method_name = get_right_method_name;
        jmethodID const pair_get_right_mid = _env->GetMethodID(pair_cls, get_right_method_name, pair_get_left.desc_str());
        if (pair_get_right_mid == nullptr) throw 4;

        serialized_module_paths = _env->CallObjectMethod(pair_sp.get(), pair_get_right_mid);
        was_exception_raised = _env->ExceptionCheck() != JNI_FALSE;
      }
    }
  }

  // restore state of these local variables
  _class_name  = class_name_sav;
  _method_name = method_name_sav;

  return std::make_tuple(module_layer_sp.release(),
                         serialized_module_paths != nullptr ? serialized_module_paths : _env->NewByteArray(0),
                         was_exception_raised);
}

/**
 * A helper method that is called to do a pre-initialization phase in child worker Java JVM
 * instance prior to invoking the child worker sub-command entry point method. Any by-int-value
 * exceptions thrown are handled in the function invoke_java_method(..).
 *
 * @return returns a bool flag that will indicate if an Java exception has been thrown
 */
bool jvm_pre_init_ctx::pre_init_for_child_worker_jvm() {
  DECL_DEFER_SMART_PTR(defer_jobj_t, defer_jobj);

  const auto rtn = shm::read_access(); // get client access of shared memory (throws shared_mem_exception if fails)
  auto const shm_base = std::get<0>(rtn);
  const auto shm_size = std::get<1>(rtn);
  using shm_t = std::remove_pointer<decltype(shm_base)>::type;
  auto const cleanup_shm_client = [shm_size](shm_t *p) { cmd_dsp::unmap_shm_client(p, shm_size); };
  std::unique_ptr<shm_t, decltype(cleanup_shm_client)> raii_shm_data_sp(shm_base, cleanup_shm_client);

  const auto block_size = *reinterpret_cast<const int32_t*>(shm_base);
  auto const byte_mem_buf = reinterpret_cast<const jbyte*>(reinterpret_cast<jbyte*>(shm_base) + sizeof(block_size));

  bool was_exception_raised = false;

  if (block_size > 0) {
    jbyteArray const ser_module_paths = _env->NewByteArray(block_size);
    defer_jobj_t ser_module_paths_sp{ ser_module_paths, defer_jobj };
    was_exception_raised = _env->ExceptionCheck() != JNI_FALSE;

    if (!was_exception_raised && ser_module_paths != nullptr) {
      _env->SetByteArrayRegion(ser_module_paths, 0, block_size, byte_mem_buf);

      const methodDescriptor childWorkerJvmBootStrap{make_childWorkerJvmBootStrap_descriptor()};
      const char *const class_name = strdupa(childWorkerJvmBootStrap.c_str());
      const char *const method_name = split_method_name_from_class_name(class_name, childWorkerJvmBootStrap.c_str());

      // save state of these local variables (may be referenced in caught exception handling code below)
      const char * const class_name_sav  = _class_name;
      const char * const method_name_sav = _method_name;

      _class_name = class_name;
      jclass const cls = _env->FindClass(class_name);
      if (cls == nullptr) throw 3;

      _method_name = method_name;
      jmethodID const mid = _env->GetStaticMethodID(cls, method_name, childWorkerJvmBootStrap.desc_str());
      if (mid == nullptr) throw 4;

      const auto is_debug = static_cast<jboolean>(get_level() <= LL::DEBUG ? JNI_TRUE : JNI_FALSE);

      jobject const module_layer = _env->CallStaticObjectMethod(cls, mid, ser_module_paths, is_debug);
      defer_jobj_t module_layer_sp{ module_layer, defer_jobj };
      was_exception_raised = _env->ExceptionCheck() != JNI_FALSE;

      if (!was_exception_raised && module_layer_sp) {
        was_exception_raised = set_system_boot_layer(module_layer_sp.get());
      }

      // restore state of these local variables
      _class_name  = class_name_sav;
      _method_name = method_name_sav;
    }
  }

  return was_exception_raised;
}

bool jvm_pre_init_ctx::set_system_boot_layer(jobject module_layer) {
  const methodDescriptor sys_bootLayer_field{ make_System_bootLayer_descriptor() };
  const char *const sys_class_name = strdupa(sys_bootLayer_field.c_str());
  const char *const bootLayer_fld = split_method_name_from_class_name(sys_class_name, sys_bootLayer_field.c_str());

  // save state of these local variables (may be referenced in caught exception handling code below)
  const char * const class_name_sav  = _class_name;
  const char * const method_name_sav = _method_name;

  _class_name = sys_class_name;
  jclass const sys_cls = _env->FindClass(sys_class_name);
  if (sys_cls == nullptr) throw 3;

  _method_name = bootLayer_fld;
  auto const bootLayer_field_id = _env->GetStaticFieldID(sys_cls, bootLayer_fld, sys_bootLayer_field.desc_str());
  if (bootLayer_field_id == nullptr) throw 4;

  _env->SetStaticObjectField(sys_cls, bootLayer_field_id, module_layer);

  // restore state of these local variables
  _class_name  = class_name_sav;
  _method_name = method_name_sav;

  return _env->ExceptionCheck() != JNI_FALSE;
}