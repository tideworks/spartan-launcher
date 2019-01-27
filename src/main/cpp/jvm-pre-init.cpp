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
#include "process-cmd-dispatch-info.h"
#include "jvm-pre-init.h"
#include "log.h"

using namespace logger;
using namespace jvm_pre_init;

/**
 * The below class methods are only invoked as helper methods from the
 * spartan invoke_java_method(..) function.
 */


methodDescriptor jvm_pre_init_ctx::make_obtainSerializedAnnotationInfo_descriptor() {
  return methodDescriptor(
      "spartan/CommandDispatchInfo/obtainSerializedSysPropertiesAndAnnotationInfo",
      "()[B",
      true, WM::GET_CMD_DISPATCH_INFO);
}

methodDescriptor jvm_pre_init_ctx::make_setSerializedAnnotationInfo_descriptor() {
  return methodDescriptor(
      "spartan/CommandDispatchInfo/setSerializedSysPropertiesAndAnnotationInfo",
      "()[B",
      true, WM::NONE);
}

/**
 * A helper method that is called by invoke_java_method(..) to set the system class loader
 * as the current thread class loader. Any by-int-value exceptions thrown are handled in
 * the function invoke_java_method(..).
 *
 * @param class_name set the name of a class to be searched for into this variable
 * so that if an exception is thrown, the handler will have access to its name
 * @param method_name  set the name of a method to be searched for into this variable
 * so that if an exception is thrown, the handler will have access to its name
 * @param env the JNI function calling context
 */
void jvm_pre_init_ctx::set_thread_class_loader_context() {
  auto const defer_jobj = [this](jobject p) {
    if (p != nullptr) {
      this->_env->DeleteLocalRef(p);
    }
  };
  using defer_jobj_t = std::unique_ptr<_jobject, decltype(defer_jobj)>;

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
  defer_jobj_t spClsLdrObj(_env->CallStaticObjectMethod(clsLdr_cls, getSysClsLdr), defer_jobj);

  _class_name = "java/lang/Thread";
  jclass const thrd_cls = _env->FindClass(_class_name);
  if (thrd_cls == nullptr) throw 3;
  _method_name = "currentThread";
  jmethodID const currThrd = _env->GetStaticMethodID(thrd_cls, _method_name, "()Ljava/lang/Thread;");
  if (currThrd == nullptr) throw 4;

  // get current thread object
  defer_jobj_t spCurrThrdObj(_env->CallStaticObjectMethod(thrd_cls, currThrd), defer_jobj);

  _method_name = "setContextClassLoader";
  jmethodID const setCntxClsLdr = _env->GetMethodID(thrd_cls, _method_name, "(Ljava/lang/ClassLoader;)V");
  if (setCntxClsLdr == nullptr) throw 4;

  // now set the system class loader on the current thread object as the thread's context class loader
  _env->CallVoidMethod(spCurrThrdObj.get(), setCntxClsLdr, spClsLdrObj.get());

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
 * will be locatable via the JVM -Xbootclasspath/a: classpath.
 * @param ss the Spartan runtime session state that is to be initialized by this helper method.
 * @return returns the shared memory allocator where Spartan runtime session state was persisted;
 * also returns a bool flag that will indicated if an Java exception has been thrown (the allocator
 * will be a nullptr if an exception was thrown)
 */
std::pair<shm::ShmAllocator *, bool> jvm_pre_init_ctx::pre_init_for_supervisor_jvm(
    const methodDescriptorBase &method_descriptor, sessionState &ss)
{
  jclass const jcls = _env->FindClass(_class_name);
  if (jcls == nullptr) throw 3;

  jmethodID const get_cmd_dispatch_info = _env->GetStaticMethodID(jcls, _method_name, method_descriptor.desc_str());
  if (get_cmd_dispatch_info == nullptr) throw 4;

  log(LL::DEBUG, "%s() invoking static method \"%s\"", __FUNCTION__, method_descriptor.c_str());
  auto const ser_cmd_dispatch_info = _env->CallStaticObjectMethod(jcls, get_cmd_dispatch_info);
  const bool was_exception_raised  = _env->ExceptionCheck() != JNI_FALSE;

  if (!was_exception_raised) {
    auto pshm = cmd_dsp::CmdDispatchInfoProcessor{_env, _class_name, _method_name, jcls, ss}
                              .process_initial_cmd_dispatch_info(reinterpret_cast<jbyteArray>(ser_cmd_dispatch_info));
    return std::make_pair(pshm, was_exception_raised);
  }

  return std::make_pair(nullptr, was_exception_raised);
}

/**
 * A helper method that is called to do a pre-initialization phSpartan service main(..)ase in child worker Java JVM
 * instance prior to invoking the child worker sub-command entry point method. Any by-int-value exceptions thrown
 * are handled in the function invoke_java_method(..).
 */
void jvm_pre_init_ctx::pre_init_for_child_worker_jvm() {
  methodDescriptor setSerializedAnnotationInfo = make_setSerializedAnnotationInfo_descriptor();
  sessionState shm_session{};
  cmd_dsp::get_cmd_dispatch_info(shm_session);

}