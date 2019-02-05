/* jvm-pre-init.h

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
#ifndef JVM_PRE_INIT_H
#define JVM_PRE_INIT_H

#include <utility>
#include "session-state.h"

// forward declarations
struct JNIEnv_;
using JNIEnv = JNIEnv_;
namespace shm {
  class ShmAllocator;
}
class methodDescriptorBase;
struct sessionState;

namespace jvm_pre_init {

  /**
   * Methods of this class are only invoked as helper methods from the Spartan
   * invoke_java_method(..) function.
   */
  class jvm_pre_init_ctx {
  private:
    JNIEnv *const _env;
    const char *&_class_name;
    const char *&_method_name;
  public:
    /**
     * Constructor.
     *
     * @param env the JNI function calling context
     *
     * @param class_name set the name of a class to be searched for into this variable
     * so that if an exception is thrown, the invoke_java_method(..) catch handler will
     * have access to its name for error logging
     *
     * @param method_name set the name of a method to be searched for into this variable
     * so that if an exception is thrown, the invoke_java_method(..) catch handler will
     * have access to its name for error logging
     */
    jvm_pre_init_ctx(JNIEnv *env, const char *&class_name, const char *&method_name)
        : _env(env), _class_name(class_name), _method_name(method_name) {}
    jvm_pre_init_ctx() = delete;
    jvm_pre_init_ctx(const jvm_pre_init_ctx &) = delete;
    jvm_pre_init_ctx(jvm_pre_init_ctx &&) = delete;
    jvm_pre_init_ctx &operator=(const jvm_pre_init_ctx &) = delete;
    jvm_pre_init_ctx &operator=(jvm_pre_init_ctx &&) = delete;
    ~jvm_pre_init_ctx() = default;
  public:
    static const char* split_method_name_from_class_name(const char *stfbuf, const char *full_method_name);
    static methodDescriptor make_obtainSerializedAnnotationInfo_descriptor();
    /**
     * A helper method that is called by invoke_java_method(..) to set the system class loader
     * as the current thread class loader. Any by-int-value exceptions thrown are handled in
     * the function invoke_java_method(..).
     */
    void set_thread_class_loader_context();
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
    std::pair<shm::ShmAllocator *, bool> pre_init_for_supervisor_jvm(const methodDescriptorBase &method_descriptor,
                                                                     sessionState &ss);
    /**
     * A helper method that is called to do a pre-initialization phase in child worker Java JVM
     * instance prior to invoking the child worker sub-command entry point method. Any by-int-value
     * exceptions thrown are handled in the function invoke_java_method(..).
     *
     * @return returns a bool flag that will indicate if an Java exception has been thrown
     */
    bool pre_init_for_child_worker_jvm();
  private:
    std::tuple<jobject, jobject, bool> invoke_supervisor_jvm_bootstrap();
    bool set_system_boot_layer(jobject module_layer);
  };

} // namespace jvm_pre_init

#endif //JVM_PRE_INIT_H