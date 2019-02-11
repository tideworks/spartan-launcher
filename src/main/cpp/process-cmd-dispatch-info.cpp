/* process-cmd-dispatch-info.cpp

Copyright 2017 - 2018 Tideworks Technology
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
#include <memory>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <algorithm>
#include "log.h"
#include "session-state.h"
#include "str-split.h"
#include "streambuf-wrapper.h"
#include "process-cmd-dispatch-info.h"

//#undef NDEBUG // uncomment this line to enable asserts in use below
#include <cassert>

using namespace logger;

extern const char * progname();

namespace cmd_dsp {

  // forward declarations section
  static std::vector<char> serialize_session_state_to_membuf(const sessionState &ss);

  // implementation section
  static const char* const unexpected_err_fmt = "%s(): %d: unexpected error (program terminating):\n";
  static const char* const java_string_descriptor = "Ljava/lang/String;";
  static const char* const java_string_array_descriptor = "[Ljava/lang/String;";

  template<typename T>
  using defer_jobj_sp_t = std::unique_ptr<_jobject, T>;

  using jstr_t = struct {
    jboolean isCopy;
    jstring j_str;
    const char *c_str;
  };

  template<typename T>
  using defer_jstr_sp_t = std::unique_ptr<jstr_t, T>;

  static void cleanup_jstr_t(JNIEnv *env, jstr_t *p) {
    assert(p != nullptr);  // check is only present when NDEBUG is not defined (i.e., production builds)
    if (p != nullptr) {
      if (p->isCopy == JNI_TRUE) {
        env->ReleaseStringUTFChars(p->j_str, p->c_str);
      }
      env->DeleteLocalRef(p->j_str);
    }
  }

  const char* split_method_name_from_class_name(const char* const stfbuf, const char* const full_method_name) {
    char * str = const_cast<char*>(strrchr(stfbuf, '/'));
    if (str == nullptr || *(str + 1) == '\0') throw std::string(full_method_name);
    *str = '\0';  // null terminate the class name string
    return ++str; // return the method name string
  }

  static methodDescriptor make_deserializeSystemProperties_descriptor() {
    return methodDescriptor(
        "spartan_startup/CommandDispatchInfo/deserializeSystemProperties",
        "([B)[Ljava/lang/String;",
        true, WM::NONE);
  }

  static methodDescriptor make_deserializeToAnnotationInfo_descriptor() {
    return methodDescriptor(
        "spartan_startup/CommandDispatchInfo/deserializeToAnnotationInfo",
        "([B)Lspartan_startup/CommandDispatchInfo;",
        true, WM::NONE);
  }

  static methodDescriptor make_systemClassPath_descriptor() {
    return methodDescriptor(
        "spartan_startup/CommandDispatchInfo/systemClassPath",
        java_string_array_descriptor,
        false, WM::NONE);
  }

  static methodDescriptor make_spartanSupervisorCommands_descriptor() {
    return methodDescriptor(
        "spartan_startup/CommandDispatchInfo/spartanSupervisorCommands",
        "[Lspartan_startup/CommandDispatchInfo$CmdInfo;",
        false, WM::NONE);
  }

  static methodDescriptor make_spartanChildWorkerCommands_descriptor() {
    return methodDescriptor(
        "spartan_startup/CommandDispatchInfo/spartanChildWorkerCommands",
        "[Lspartan_startup/CommandDispatchInfo$ChildCmdInfo;",
        false, WM::NONE);
  }

  static methodDescriptor make_spartanMainEntryPoint_descriptor() {
    return methodDescriptor(
        "spartan_startup/CommandDispatchInfo/spartanMainEntryPoint",
        "Lspartan_startup/CommandDispatchInfo$MethInfo;",
        false, WM::NONE);
  }

  std::pair<shm::ShmAllocator*, bool> CmdDispatchInfoProcessor::process_initial_cmd_dispatch_info(
      jbyteArray ser_module_paths, jbyteArray ser_cmd_dispatch_info)
  {
    DECL_DEFER_SMART_PTR(defer_jobj_t, defer_jobj);

#ifdef _DEBUG
    debug_dump_sessionState(ss, 'B');
#endif

    // save state of these instance variables (may be referenced in caught exception handling in invoke_java_method())
    const char* const class_name_sav  = _class_name;
    const char* const method_name_sav = _method_name;

    const methodDescriptor deserSysProps{ make_deserializeSystemProperties_descriptor() };
    const char* const deserSysProps_cls_name = strdupa(deserSysProps.c_str());
    const char* const deserSysProps_mth_name = split_method_name_from_class_name(deserSysProps_cls_name,
                                                                                 deserSysProps.c_str());

    _method_name = deserSysProps_mth_name;
    auto const deserialize_sys_props = _env->GetStaticMethodID(cls, deserSysProps_mth_name, deserSysProps.desc_str());
    if (deserialize_sys_props == nullptr) throw 4;

    int line_nbr = __LINE__ + 1;
    auto const sys_prop_strs = _env->CallStaticObjectMethod(cls, deserialize_sys_props, ser_cmd_dispatch_info);
    defer_jobj_t sp_sys_prop_strs(sys_prop_strs, defer_jobj);
    bool was_exception_raised = _env->ExceptionCheck() != JNI_FALSE;
    if (was_exception_raised) {
      log(LL::FATAL, unexpected_err_fmt, __FUNCTION__, line_nbr);
      return std::make_pair(nullptr, was_exception_raised);
    }

    const methodDescriptor deserToAnnotationInfo{ make_deserializeToAnnotationInfo_descriptor() };
    const char* const deserToAnnotationInfo_cls_name = strdupa(deserToAnnotationInfo.c_str());
    const char* const deserToAnnotationInfo_mth_name = split_method_name_from_class_name(deserToAnnotationInfo_cls_name,
                                                                                         deserToAnnotationInfo.c_str());

    _method_name = deserToAnnotationInfo_mth_name;
    auto const deserialize_cmd_dsp = _env->GetStaticMethodID(cls, deserToAnnotationInfo_mth_name,
                                                                  deserToAnnotationInfo.desc_str());
    if (deserialize_cmd_dsp == nullptr) throw 4;

    line_nbr = __LINE__ + 1;
    auto const cmd_dispatch_info = _env->CallStaticObjectMethod(cls, deserialize_cmd_dsp, ser_cmd_dispatch_info);
    defer_jobj_t sp_cmd_dispatch_info(cmd_dispatch_info, defer_jobj);
    was_exception_raised = _env->ExceptionCheck() != JNI_FALSE;
    if (was_exception_raised) {
      log(LL::FATAL, unexpected_err_fmt, __FUNCTION__, line_nbr);
      return std::make_pair(nullptr, was_exception_raised);
    }

    // restore state of these instance variables
    _class_name  = class_name_sav;
    _method_name = method_name_sav;

#ifdef _DEBUG
    debug_dump_dispatch_info(sp_cmd_dispatch_info.get());
#endif

    methodDescriptor mainEntryPoint_save = ss.spartanMainEntryPoint;
    apply_cmd_dsp_info_to_session_state(sp_sys_prop_strs.get(), sp_cmd_dispatch_info.get());
    if (!mainEntryPoint_save.empty()) {
      // config.ini mainEntry specifier takes precedence over Java @SupervisorMain annotation, i.e., acts as an override
      ss.spartanMainEntryPoint = std::move(mainEntryPoint_save);
    }

#ifdef _DEBUG
    debug_dump_sessionState(ss, 'C');
#endif

    const auto ss_ser_membuf = serialize_session_state_to_membuf(ss);

    const auto pg_size = sysconf(_SC_PAGE_SIZE);
    const auto byteArrayOneLen = _env->GetArrayLength(ser_module_paths);
    const auto byteArrayTwoLen = _env->GetArrayLength(ser_cmd_dispatch_info);
    const auto shm_required_size =  sizeof(int32_t) + byteArrayOneLen +
                                    sizeof(int32_t) + byteArrayTwoLen +
                                    sizeof(int32_t) + ss_ser_membuf.size();
    auto pgs = static_cast<int>(shm_required_size / pg_size);
    if ((shm_required_size % pg_size) != 0) pgs++;

    using shm_alloc_t = shm::ShmAllocator;
    auto const cleanup_shm_alloc = [](shm_alloc_t *p) { ::delete p; };
    std::unique_ptr<shm_alloc_t, decltype(cleanup_shm_alloc)> sp_shm_alloc{ shm::make(pgs), cleanup_shm_alloc };
    auto const byte_mem_buffer = new(*sp_shm_alloc) jbyte[shm_required_size];
    auto byte_mem_buf_pos = byte_mem_buffer;

    // append serialized module paths byte array
    *reinterpret_cast<int32_t*>(byte_mem_buf_pos) = byteArrayOneLen;
    byte_mem_buf_pos += sizeof(int32_t);
    _env->GetByteArrayRegion(ser_module_paths, 0, byteArrayOneLen, byte_mem_buf_pos);
    byte_mem_buf_pos += byteArrayOneLen;

    // append serialized command dispatch info byte array
    *reinterpret_cast<int32_t*>(byte_mem_buf_pos) = byteArrayTwoLen;
    byte_mem_buf_pos += sizeof(int32_t);
    _env->GetByteArrayRegion(ser_cmd_dispatch_info, 0, byteArrayTwoLen, byte_mem_buf_pos);
    byte_mem_buf_pos += byteArrayTwoLen;

    // append serialized session state byte array
    *reinterpret_cast<int32_t*>(byte_mem_buf_pos) = static_cast<int32_t>(ss_ser_membuf.size());
    byte_mem_buf_pos += sizeof(int32_t);
    memcpy(byte_mem_buf_pos, &ss_ser_membuf.front(), ss_ser_membuf.size());

#ifdef _DEBUG
    log(LL::DEBUG, "\tpage size: %ld, pages: %d, required byte array size: %d\n"
                   "\t\tshared memory buffer size: %lu, utilized memory size %lu\n",
        pg_size, pgs, shm_required_size, std::get<1>(sp_shm_alloc->getMemBuf()),
        std::get<1>(sp_shm_alloc->getUtilizedMemBuf()));
#endif

    return std::make_pair(sp_shm_alloc.release(), was_exception_raised);
  }

#ifdef _DEBUG
  void CmdDispatchInfoProcessor::debug_dump_dispatch_info(jobject cmd_dispatch_info) {
    if (logger::get_level() > LL::DEBUG) return;
    const char* const method_name_sav = _method_name;
    _method_name = "toString";
    jmethodID const to_string_method = _env->GetMethodID(cls, _method_name, "()Ljava/lang/String;");
    if (to_string_method == nullptr) throw 4;
    auto result = reinterpret_cast<jstring>(_env->CallObjectMethod(cmd_dispatch_info, to_string_method));
    jstr_t jstr{ JNI_FALSE, result, nullptr };
    jstr.c_str = _env->GetStringUTFChars(jstr.j_str, &jstr.isCopy);
    auto const defer_cleanup_jstr = [this](jstr_t *p) { cleanup_jstr_t(_env, p); };
    defer_jstr_sp_t<decltype(defer_cleanup_jstr)> sp_cmd_dsp_info(&jstr, defer_cleanup_jstr);
    logm(LL::DEBUG, sp_cmd_dsp_info->c_str);
    _method_name = method_name_sav;
  }
#endif

  static void process_jstring_array(JNIEnv *env, jobjectArray jstr_array,
                                    const std::function<void(int)> &prepare,
                                    const std::function<void(std::string &)> &action)
  {
    const auto array_len = env->GetArrayLength(jstr_array);
    if (array_len > 0) {
      prepare(array_len);
      auto const defer_cleanup_jstr = [env](jstr_t *p) { cleanup_jstr_t(env, p); };
      jstr_t jstr{JNI_FALSE, nullptr, nullptr};
      std::string str;
      for (int i = 0; i < array_len; i++) {
        jstr.isCopy = JNI_FALSE;
        jstr.j_str = reinterpret_cast<jstring>(env->GetObjectArrayElement(jstr_array, i));
        jstr.c_str = env->GetStringUTFChars(jstr.j_str, &jstr.isCopy);
        defer_jstr_sp_t<decltype(defer_cleanup_jstr)> sp_jstr(&jstr, defer_cleanup_jstr);
        str = sp_jstr->c_str;
        action(str);
      }
    }
  }

  void CmdDispatchInfoProcessor::apply_cmd_dsp_info_to_session_state(jobject sys_prop_strs, jobject cmd_dispatch_info) {
    DECL_DEFER_SMART_PTR(defer_jobj_t, defer_jobj);

    auto const sys_prop_strs_array = reinterpret_cast<jobjectArray>(sys_prop_strs);
    process_jstring_array(_env, sys_prop_strs_array,
                          [this](int array_len) {
                            log(LL::DEBUG, "sys_prop_strs_array length: %d", array_len);
                            auto const pvec = new std::vector<std::string>();
                            pvec->reserve(static_cast<size_t >(array_len));
                            ss.spSerializedSystemProperties.reset(pvec);
                          },
                          [this](std::string &str) { ss.spSerializedSystemProperties->push_back(std::move(str)); });

    const methodDescriptor sysClsPath{ make_systemClassPath_descriptor() };
    const char* const sysClsPath_cls_name = strdupa(sysClsPath.c_str());
    const char* const sysClsPath_mth_name = split_method_name_from_class_name(sysClsPath_cls_name, sysClsPath.c_str());

    auto field_id = _env->GetFieldID(cls, sysClsPath_mth_name, sysClsPath.desc_str());
#ifdef NDEBUG
    if (field_id == nullptr) throw -1;
#else
    assert(field_id != nullptr);
#endif

    {
      defer_jobj_t sp_cls_path_array(_env->GetObjectField(cmd_dispatch_info, field_id), defer_jobj);

      std::string cls_path_str;
      cls_path_str.reserve(2048);
      auto const cls_path_array = reinterpret_cast<jobjectArray>(sp_cls_path_array.get());
      process_jstring_array(_env, cls_path_array,
                            [](int array_len) {},
                            [&cls_path_str](std::string &str) {
                              if (!cls_path_str.empty()) {
                                cls_path_str += ':';
                              }
                              cls_path_str += str;
                            });
      ss.systemClassPath = std::move(cls_path_str);
      log(LL::DEBUG, "CLASSPATH: %s", ss.systemClassPath.c_str());
    }

    if (ss.spartanMainEntryPoint.empty()) {
      extract_main_entry_method_info(cmd_dispatch_info);
    }

    {
      const methodDescriptor sprtnSupervisorCmds { make_spartanSupervisorCommands_descriptor() };
      const char* const sprtnSupervisorCmds_cls_name = strdupa(sprtnSupervisorCmds.c_str());
      const char* const sprtnSupervisorCmds_mth_name = split_method_name_from_class_name(sprtnSupervisorCmds_cls_name,
                                                                                         sprtnSupervisorCmds.c_str());

      field_id = _env->GetFieldID(cls, sprtnSupervisorCmds_mth_name, sprtnSupervisorCmds.desc_str());
#ifdef NDEBUG
      if (field_id == nullptr) throw -1;
#else
      assert(field_id != nullptr);
#endif

      defer_jobj_t sp_supervisor_cmds(_env->GetObjectField(cmd_dispatch_info, field_id), defer_jobj);
      auto const supervisor_cmds_array = reinterpret_cast<jobjectArray>(sp_supervisor_cmds.get());
      const auto array_len = _env->GetArrayLength(supervisor_cmds_array);
      if (array_len > 0) {
        log(LL::DEBUG, "supervisor_cmds_array length: %d", array_len);
        auto const pvec = new std::vector<methodDescriptorCmd>();
        pvec->reserve(static_cast<size_t >(array_len));
        ss.spSpartanSupervisorCommands.reset(pvec);
        // setup this->_class_name to reference the Java class for accessing a supervisor CmdInfo
        const char* const cls_name_sav = _class_name;
        _class_name = "spartan_startup/CommandDispatchInfo$CmdInfo";
        std::string method_name_str, descriptor_str, command_str;
        for (int i = 0; i < array_len; i++) {
          method_name_str.clear();
          descriptor_str.clear();
          auto const supervisor_cmd_jobj = _env->GetObjectArrayElement(supervisor_cmds_array, i);
          defer_jobj_t sp_supervisor_cmd(supervisor_cmd_jobj, defer_jobj);
          auto const cmd_info_cls = extract_method_info(sp_supervisor_cmd.get(),
                                                        [&method_name_str, &descriptor_str]
                                                            (std::string &method_name, std::string &descriptor)
                                                        {
                                                          method_name_str = std::move(method_name);
                                                          descriptor_str  = std::move(descriptor);
                                                        });
          command_str.clear();
          extract_method_cmd_info(cmd_info_cls, sp_supervisor_cmd.get(), [&command_str](std::string &command) {
            command_str = std::move(command);
          });
          ss.spSpartanSupervisorCommands->emplace_back(std::move(method_name_str), std::move(descriptor_str),
                                                       std::move(command_str), false, WM::SUPERVISOR_DO_CMD);
        }
        _class_name = cls_name_sav;
      }
    }

    {
      const methodDescriptor sprtnChildWrkrCmds{ make_spartanChildWorkerCommands_descriptor() };
      const char* const sprtnChildWrkrCmds_cls_name = strdupa(sprtnChildWrkrCmds.c_str());
      const char* const sprtnChildWrkrCmds_mth_name = split_method_name_from_class_name(sprtnChildWrkrCmds_cls_name,
                                                                                        sprtnChildWrkrCmds.c_str());

      field_id = _env->GetFieldID(cls, sprtnChildWrkrCmds_mth_name, sprtnChildWrkrCmds.desc_str());
#ifdef NDEBUG
      if (field_id == nullptr) throw -1;
#else
      assert(field_id != nullptr);
#endif

      defer_jobj_t sp_child_worker_cmds(_env->GetObjectField(cmd_dispatch_info, field_id), defer_jobj);
      auto const child_worker_cmds_array = reinterpret_cast<jobjectArray>(sp_child_worker_cmds.get());
      const auto array_len = _env->GetArrayLength(child_worker_cmds_array);
      if (array_len > 0) {
        log(LL::DEBUG, "child_worker_cmds_array length: %d", array_len);
        auto const pvec = new std::vector<methodDescriptorCmd>();
        pvec->reserve(static_cast<size_t >(array_len));
        ss.spSpartanChildProcessorCommands.reset(pvec);
        // setup this->_class_name to reference the Java class for accessing a child worker ChildCmdInfo
        const char* const cls_name_sav = _class_name;
        _class_name = "spartan_startup/CommandDispatchInfo$ChildCmdInfo";
        std::string method_name_str, descriptor_str, command_str, jvm_optns_str;
        for (int i = 0; i < array_len; i++) {
          defer_jobj_t sp_child_worker_cmd(_env->GetObjectArrayElement(child_worker_cmds_array, i), defer_jobj);
          method_name_str.clear();
          descriptor_str.clear();
          auto const cmd_info_cls = extract_method_info(sp_child_worker_cmd.get(),
                                                        [&method_name_str, &descriptor_str]
                                                            (std::string &method_name, std::string &descriptor) {
                                                          method_name_str = std::move(method_name);
                                                          descriptor_str = std::move(descriptor);
                                                        });
          command_str.clear();
          extract_method_cmd_info(cmd_info_cls, sp_child_worker_cmd.get(), [&command_str](std::string &command) {
            command_str = std::move(command);
          });
          jvm_optns_str.clear();
          extract_method_jvm_optns_cmd_info(cmd_info_cls, sp_child_worker_cmd.get(),
                                            [&jvm_optns_str](std::string &jvm_optns) {
                                              jvm_optns_str = std::move(jvm_optns);
                                            });
          ss.spSpartanChildProcessorCommands->emplace_back(std::move(method_name_str), std::move(descriptor_str),
                                                           std::move(command_str), std::move(jvm_optns_str),
                                                           true, WM::CHILD_DO_CMD);
        }
        _class_name = cls_name_sav;
      }
    }
  }

  void CmdDispatchInfoProcessor::extract_main_entry_method_info(jobject cmd_dispatch_info) {
    DECL_DEFER_SMART_PTR(defer_jobj_t, defer_jobj);

    const methodDescriptor spartanMainEntryPoint{ make_spartanMainEntryPoint_descriptor() };
    const char* const spartanMainEntryPoint_cls_name = strdupa(spartanMainEntryPoint.c_str());
    const char* const spartanMainEntryPoint_mth_name = split_method_name_from_class_name(spartanMainEntryPoint_cls_name,
                                                                                         spartanMainEntryPoint.c_str());

    auto field_id = _env->GetFieldID(cls, spartanMainEntryPoint_mth_name, spartanMainEntryPoint.desc_str());
#ifdef NDEBUG
    if (field_id == nullptr) throw -1;
#else
    assert(field_id != nullptr);
#endif

    defer_jobj_t sp_main_entry_method_info(_env->GetObjectField(cmd_dispatch_info, field_id), defer_jobj);
    if (!sp_main_entry_method_info) {
      std::string err_msg{"no main() entry method defined in either config.ini or via @SupervisorMain annotation"};
      throw invalid_initialization_exception(std::move(err_msg));
    }

    // setup this->_class_name to reference the Java class for accessing a plain MethInfo
    const char* const cls_name_sav = _class_name;
    _class_name = "spartan_startup/CommandDispatchInfo$MethInfo";
    extract_method_info(sp_main_entry_method_info.get(), [this](std::string &method_name, std::string &descriptor) {
      methodDescriptor main_entry(std::move(method_name), std::move(descriptor), true, WhichMethod::MAIN);
      ss.spartanMainEntryPoint = std::move(main_entry);
    });
    _class_name = cls_name_sav;

    // extract the class name from the fully qualified main entry point method name
    std::string cls_name_str = [](const char *const full_entry_point_name) -> std::string {
      const char *class_name_cstr = strdupa(full_entry_point_name);
      auto str = const_cast<char *>(strrchr(class_name_cstr, '/'));
      if (str == nullptr || *(str + 1) == '\0') return std::string();
      *(str + 1) = '\0';  // null terminate the class name string (now refers to just the class)
      return std::string(class_name_cstr);
    }(ss.spartanMainEntryPoint.c_str());

    // adjust the class name of other Spartan standard entry points
    std::vector<methodDescriptor *> other_spartan_entry_methods = {&ss.spartanGetStatusEntryPoint,
                                                                   &ss.spartanSupervisorShutdownEntryPoint,
                                                                   &ss.spartanChildNotifyEntryPoint,
                                                                   &ss.spartanChildCompletionNotifyEntryPoint,
                                                                   &ss.spartanSupervisorEntryPoint};

    const std::string entry_points_class = cls_name_str.empty() ?
                                           std::move(std::string("spartan/SpartanBase/")) : std::move(cls_name_str);

    for (auto pentry_method : other_spartan_entry_methods) {
      if (strncasecmp(pentry_method->fullMethodName.c_str(), entry_points_class.c_str(),
                      entry_points_class.length()) != 0) {
        auto parts = str_split(pentry_method->fullMethodName.c_str(), '/');
        assert(parts.size() >= 2); // should be minimum of 2 parts
        if (parts.empty()) continue;
        const auto last_elm = parts.size() - 1;
        pentry_method->fullMethodName = entry_points_class;
        pentry_method->fullMethodName += parts[last_elm];
      }
    }
  }

  jclass CmdDispatchInfoProcessor::extract_method_info(jobject method_info,
                                                       const std::function<void(std::string&, std::string&)> &action)
  {
    jclass const meth_info_cls = _env->FindClass(_class_name); // was set by caller to appropriate MethInfo class name
    if (cls == nullptr) throw 3;

    auto field_id = _env->GetFieldID(meth_info_cls, "className", java_string_descriptor);
#ifdef NDEBUG
    if (field_id == nullptr) throw -1;
#else
    assert(field_id != nullptr);
#endif

    std::string main_entry_str, descriptor_str;

    auto const defer_cleanup_jstr = [this](jstr_t *p) { cleanup_jstr_t(_env, p); };
    jstr_t jstr{ JNI_FALSE, nullptr, nullptr };
    jstr.j_str = reinterpret_cast<jstring>(_env->GetObjectField(method_info, field_id));
    jstr.c_str = _env->GetStringUTFChars(jstr.j_str, &jstr.isCopy);
    defer_jstr_sp_t<decltype(defer_cleanup_jstr)> sp_jstr(&jstr, defer_cleanup_jstr);
    main_entry_str = sp_jstr->c_str;
    (void) sp_jstr.release();
    replace(main_entry_str.begin(), main_entry_str.end(), '.', '/');
    main_entry_str += '/';

    field_id = _env->GetFieldID(meth_info_cls, "methodName", java_string_descriptor);
#ifdef NDEBUG
    if (field_id == nullptr) throw -1;
#else
    assert(field_id != nullptr);
#endif

    jstr.isCopy = JNI_FALSE;
    jstr.c_str = nullptr;
    jstr.j_str = reinterpret_cast<jstring>(_env->GetObjectField(method_info, field_id));
    jstr.c_str = _env->GetStringUTFChars(jstr.j_str, &jstr.isCopy);
    sp_jstr.reset(&jstr);
    main_entry_str += sp_jstr->c_str;
    (void) sp_jstr.release();

    field_id = _env->GetFieldID(meth_info_cls, "descriptor", java_string_descriptor);
#ifdef NDEBUG
    if (field_id == nullptr) throw -1;
#else
    assert(field_id != nullptr);
#endif

    jstr.isCopy = JNI_FALSE;
    jstr.c_str = nullptr;
    jstr.j_str = reinterpret_cast<jstring>(_env->GetObjectField(method_info, field_id));
    jstr.c_str = _env->GetStringUTFChars(jstr.j_str, &jstr.isCopy);
    sp_jstr.reset(&jstr);
    descriptor_str = sp_jstr->c_str;
    (void) sp_jstr.release();

    action(main_entry_str, descriptor_str);

    return meth_info_cls;
  }

  void CmdDispatchInfoProcessor::extract_method_cmd_info(jclass cmd_info_cls, jobject method_cmd_info,
                                                         const std::function<void(std::string &command)> &action)
  {
    const auto field_id = _env->GetFieldID(cmd_info_cls, "cmd", java_string_descriptor);
#ifdef NDEBUG
    if (field_id == nullptr) throw -1;
#else
    assert(field_id != nullptr);
#endif

    auto const defer_cleanup_jstr = [this](jstr_t *p) { cleanup_jstr_t(_env, p); };
    jstr_t jstr{ JNI_FALSE, nullptr, nullptr };
    jstr.j_str = reinterpret_cast<jstring>(_env->GetObjectField(method_cmd_info, field_id));
    jstr.c_str = _env->GetStringUTFChars(jstr.j_str, &jstr.isCopy);
    defer_jstr_sp_t<decltype(defer_cleanup_jstr)> sp_jstr(&jstr, defer_cleanup_jstr);

    std::string command(sp_jstr->c_str);
    action(command);
  }

  void CmdDispatchInfoProcessor::extract_method_jvm_optns_cmd_info(jclass cmd_info_cls, jobject method_cmd_info,
                                                                   const std::function<void(std::string &)> &action)
  {
    DECL_DEFER_SMART_PTR(defer_jobj_t, defer_jobj);

    const auto field_id = _env->GetFieldID(cmd_info_cls, "jvmArgs", java_string_array_descriptor);
#ifdef NDEBUG
    if (field_id == nullptr) throw -1;
#else
    assert(field_id != nullptr);
#endif

    std::string jvm_optns_str;

    defer_jobj_t sp_jvm_optns(_env->GetObjectField(method_cmd_info, field_id), defer_jobj);
    auto const jvm_optns_array = reinterpret_cast<jobjectArray>(sp_jvm_optns.get());
    const auto array_len = _env->GetArrayLength(jvm_optns_array);
    if (array_len > 0) {
      jvm_optns_str.reserve(2048);
      auto const defer_cleanup_jstr = [this](jstr_t *p) { cleanup_jstr_t(_env, p); };
      jstr_t jstr{JNI_FALSE, nullptr, nullptr};
      for(int i = 0; i < array_len; i++) {
        jstr.isCopy = JNI_FALSE;
        jstr.j_str = reinterpret_cast<jstring>(_env->GetObjectArrayElement(jvm_optns_array, i));
        jstr.c_str = _env->GetStringUTFChars(jstr.j_str, &jstr.isCopy);
        defer_jstr_sp_t<decltype(defer_cleanup_jstr)> sp_jstr(&jstr, defer_cleanup_jstr);
        if (!jvm_optns_str.empty()) {
          jvm_optns_str += ' ';
        }
        jvm_optns_str += sp_jstr->c_str;
      }
    }

    if (!jvm_optns_str.empty()) {
      action(jvm_optns_str);
    }
  }

  static std::vector<char> serialize_session_state_to_membuf(const sessionState &ss) {
    std::stringstream strm(std::ios_base::in | std::ios_base::out);
    strm << ss;
    const auto length = strm.tellp();
    std::vector<char> membuf(static_cast<size_t >(length));
    strm.seekg(0, std::stringstream::beg);
    strm.read(&membuf.front(), length);
    return membuf; // rely on return value optimization (compiler will select move semantics)
  }

  void unmap_shm_client(void *p, size_t shm_size) {
    if (p != nullptr) {
      shm::unmap(p, shm_size);
      log(LL::DEBUG, "pid(%d): unmapped shared memory \"/%s\": %p of size %lu", getpid(), progname(), p, shm_size);
    }
  }

  static std::tuple<const char*, int32_t> get_session_state_buf_info(void *shm_base) {
    // skip over serialized module paths byte array
    auto byte_mem_buf_pos = reinterpret_cast<const char*>(shm_base);
    auto block_size = *reinterpret_cast<const int32_t*>(byte_mem_buf_pos);
    byte_mem_buf_pos += sizeof(block_size) + block_size;

    // skip over serialized command dispatch info byte array
    block_size = *reinterpret_cast<const int32_t*>(byte_mem_buf_pos);
    byte_mem_buf_pos += sizeof(block_size) + block_size;

    // skip over serialized command dispatch info size field
    block_size = *reinterpret_cast<const int32_t*>(byte_mem_buf_pos);
    byte_mem_buf_pos += sizeof(block_size);

    // return serialized command dispatch info byte array and its size
    return std::make_tuple(byte_mem_buf_pos, block_size);
  }

  void get_cmd_dispatch_info(sessionState &ss) {
    const auto rtn = shm::read_access(); // get client access of shared memory (throws shared_mem_exception if fails)
    auto const shm_base = std::get<0>(rtn);
    const auto shm_size = std::get<1>(rtn);
    using shm_t = std::remove_pointer<decltype(shm_base)>::type;
    auto const cleanup_shm_client = [shm_size](shm_t *p) { unmap_shm_client(p, shm_size); };
    std::unique_ptr<shm_t, decltype(cleanup_shm_client)> sp_shm_data_tmp(shm_base, cleanup_shm_client);
#ifdef _DEBUG
    log(LL::DEBUG, "pid(%d): client shm base: %p of size %lu", getpid(), sp_shm_data_tmp.get(), shm_size);
#endif
    // get the sessionState shared memory buffer
    const auto ss_buf_info = get_session_state_buf_info(sp_shm_data_tmp.get());

    // instantiate the custom streambuf with the pre-existing char buffer
    streambufWrapper buf(std::get<0>(ss_buf_info), static_cast<size_t>(std::get<1>(ss_buf_info)));
    // instantiate an istream for reading from the custom streambuf
    std::istream is(&buf);

    // deserialize sessionState object from shared memory buffer
    is >> ss;

#ifdef _DEBUG
    debug_dump_sessionState(ss, 'D');
#endif
  }

  std::unordered_set<std::string> get_child_processor_commands(const sessionState &ss) {
    std::unordered_set<std::string> cmds_set(29);
    if (!ss.spartanChildProcessorCommands.empty()) {
      std::string cmds(ss.spartanChildProcessorCommands);
      std::transform(cmds.begin(), cmds.end(), cmds.begin(), ::tolower);
      auto cmds_dup = strdupa(cmds.c_str());
      static const char *const delim = ",";
      char *save = nullptr;
      cmds_set.emplace(strtok_r(cmds_dup, delim, &save));
      const char *cmd_tok = nullptr;
      while ((cmd_tok = strtok_r(nullptr, delim, &save)) != nullptr) {
        std::string cmd_tok_str(cmd_tok);
        cmds_set.emplace(std::move(cmd_tok_str));
      }
    }
    if (ss.spSpartanChildProcessorCommands) {
      for(auto &methDesc : *ss.spSpartanChildProcessorCommands) {
        std::string cmd_tok_str(methDesc.cmd_cstr());
        std::transform(cmd_tok_str.begin(), cmd_tok_str.end(), cmd_tok_str.begin(), ::tolower);
        cmds_set.emplace(std::move(cmd_tok_str));
      }
    }
    return cmds_set;
  }

} // namespace cmd_dsp