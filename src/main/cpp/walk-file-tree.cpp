#include <memory>
#include <cstring>
#include <jni.h>
#include <dirent.h>
#include <unistd.h>
#include <cxxabi.h>
#include "spartan_io_Files.h"
#include "string-view.h"
#include "findfiles.h"
#include "log.h"
#include "format2str.h"

//#undef NDEBUG
#include <cassert>

using logger::LL;
using bpstd::string_view;


// declare findfiles_exception
DECL_EXCEPTION(fsync_dir)

/*
 * Class:     spartan_io_Files
 * Method:    fsyncDirectory
 * Signature: (Ljava/lang/String;)V
 */
extern "C" JNIEXPORT void JNICALL Java_spartan_io_Files_fsyncDirectory(JNIEnv *env, jclass /*cls*/, jstring dir) {
  try {
    assert(dir != nullptr);

    // now get a c_str copy that can be used by C++ code
    struct {
      jboolean isCopy;
      jstring j_str;
      const char *c_str;
    }
        jstr{JNI_FALSE, dir, nullptr};
    jstr.c_str = env->GetStringUTFChars(jstr.j_str, &jstr.isCopy);
    assert(jstr.c_str != nullptr);

    using jstr_t = decltype(jstr);
    auto const cleanup = [env](jstr_t *p)
    {
      if (p != nullptr) {
        if (p->isCopy == JNI_TRUE) {
          env->ReleaseStringUTFChars(p->j_str, p->c_str);
        }
        env->DeleteLocalRef(p->j_str);
      }
    };
    std::unique_ptr<jstr_t, decltype(cleanup)> sp_start_dir(&jstr, cleanup);
    const string_view dir_sv{jstr.c_str}; // wrap the c_str in a string_view

    DIR * const d = opendir(dir_sv.c_str());
    if (d == nullptr) {
      const char * const err_msg_fmt = "could not open specified directory \"%s\":\n\t%s";
      throw fsync_dir_exception( format2str(err_msg_fmt, dir_sv.c_str(), strerror(errno)) );
    }
    auto const close_dir = [](DIR *pd) {
      if (pd != nullptr) {
        closedir(pd);
      }
    };
    std::unique_ptr<DIR, decltype(close_dir)> dir_sp(d, close_dir);

    const int dfd = dirfd(dir_sp.get());
    if (dfd == -1) {
      const char * const err_msg_fmt = "dirfd() could not get directory file descriptor for \"%s\":\n\t%s";
      throw fsync_dir_exception( format2str(err_msg_fmt, dir_sv.c_str(), strerror(errno)) );
    }

    // call returns after directory has been fully flushed
    auto rtn = fsync(dfd);
    if (rtn == -1) {
      const char * const err_msg_fmt = "fsync() failed for \"%s\":\n\t%s";
      throw fsync_dir_exception( format2str(err_msg_fmt, dir_sv.c_str(), strerror(errno)) );
    }
  } catch(const fsync_dir_exception &ex) {
    // TODO: need to wrap and throw this as a java.io.IOException to the Java layer
    log(LL::ERR, "process %d: spartan.io.Files.%s() %s: %s", getpid(), __func__, ex.name(), ex.what());
    _exit(EXIT_FAILURE);
  } catch(...) {
    // TODO: need to wrap error and throw an exception to the Java layer (probably should be a Runtime-derived exception)
    const auto ex_nm = get_unmangled_name(abi::__cxa_current_exception_type()->name());
    log(LL::ERR, "process %d: spartan.io.Files.%s() terminating due to unhandled exception of type %s",
        getpid(), __func__, ex_nm.c_str());
    _exit(EXIT_FAILURE);
  }
}

/*
 * Class:     spartan_io_Files
 * Method:    walk_file_tree
 * Signature: (CIZLjava/lang/String;Ljava/nio/file/FileVisitor;)V
 */
extern "C" JNIEXPORT void JNICALL
Java_spartan_io_Files_walk_1file_1tree(JNIEnv *env, jobject thisObj, jchar separator_char, jint maxdepth,
                                       jboolean follow_links, jobject start_dir, jobject visitor)
{
  find_files ff{ static_cast<char>(separator_char), maxdepth, follow_links != JNI_FALSE };

  // obtain jstring from start_dir Path object
  assert(start_dir != nullptr);
  auto path_cls = env->GetObjectClass(start_dir);
  assert(path_cls != nullptr);
  auto const toString = env->GetMethodID(path_cls, "toString", "()Ljava/lang/String;");
  assert(toString != nullptr);
  auto const start_dir_jstr = (jstring) env->CallObjectMethod(start_dir, toString);

  // now get a c_str copy that can be used by C++ code
  struct {
    jboolean isCopy;
    jstring j_str;
    const char *c_str;
  }
      jstr{ JNI_FALSE, start_dir_jstr, nullptr };
  jstr.c_str = env->GetStringUTFChars(jstr.j_str, &jstr.isCopy);
  assert(jstr.c_str != nullptr);

  using jstr_t = decltype(jstr);
  auto const cleanup = [env](jstr_t *p) {
    if (p != nullptr) {
      if (p->isCopy == JNI_TRUE) {
        env->ReleaseStringUTFChars(p->j_str, p->c_str);
      }
      env->DeleteLocalRef(p->j_str);
    }
  };
  std::unique_ptr<jstr_t, decltype(cleanup)> sp_start_dir(&jstr, cleanup);
  const string_view src_dir_sv{jstr.c_str}; // wrap the c_str in a string_view

  auto const defer_jobj = [env](jobject p)
  {
    if (p != nullptr) {
      env->DeleteLocalRef(p);
    }
  };
  std::unique_ptr<_jobject, decltype(defer_jobj)> sp_file_path_jobj{ nullptr, defer_jobj };
  std::unique_ptr<_jobject, decltype(defer_jobj)> sp_file_name_jobj{ nullptr, defer_jobj };
  std::unique_ptr<_jobject, decltype(defer_jobj)> sp_file_attr_jobj{ nullptr, defer_jobj };

  const char * class_name = "spartan.io.Files";
  const char * method_name = "makePath";

  try {
    // instantiate a file_path and file_name java.nio.file.Path objects
    auto const files_cls = env->GetObjectClass(thisObj);
    assert(files_cls != nullptr);
    if (files_cls == nullptr) throw 1;
    auto const makePath = env->GetStaticMethodID(files_cls, method_name, "(Ljava/nio/file/Path;)Ljava/nio/file/Path;");
    assert(makePath != nullptr);
    if (makePath == nullptr) throw 2;
    auto const file_path = env->CallStaticObjectMethod(files_cls, makePath, start_dir);
    assert(file_path != nullptr);
    class_name = "spartan.io.Files.Path";
    if (file_path == nullptr) throw 3;
    sp_file_path_jobj.reset(file_path);
    auto const file_name = env->CallStaticObjectMethod(files_cls, makePath, start_dir);
    assert(file_name != nullptr);
    if (file_name == nullptr) throw 3;
    sp_file_name_jobj.reset(file_name);

    path_cls = env->GetObjectClass(file_path);
    assert(path_cls != nullptr);
    if (path_cls == nullptr) throw 1;
    auto const path_init = env->GetMethodID(path_cls, method_name = "init", "(Ljava/lang/String;Ljava/nio/file/Path;)V");
    assert(path_init != nullptr);
    if (path_init == nullptr) throw 2;

    // instantiate a java.nio.file.attribute.BasicFileAttributes object
    class_name = "spartan.io.Files";
    auto const makeBasicFileAttributes = env->GetStaticMethodID(files_cls, method_name = "makeBasicFileAttributes",
                                                "(Ljava/nio/file/Path;)Ljava/nio/file/attribute/BasicFileAttributes;");
    assert(makeBasicFileAttributes != nullptr);
    if (makeBasicFileAttributes == nullptr) throw 2;
    auto const file_attributes = env->CallStaticObjectMethod(files_cls, makeBasicFileAttributes, start_dir);
    assert(file_attributes != nullptr);
    class_name = "spartan.io.Files.BasicFileAttributes";
    if (file_attributes == nullptr) throw 3;
    sp_file_attr_jobj.reset(file_attributes);

    auto const file_attrs_cls = env->GetObjectClass(file_attributes);
    assert(file_attrs_cls != nullptr);
    if (file_attrs_cls == nullptr) throw 1;
    auto const file_attrs_init = env->GetMethodID(file_attrs_cls, method_name = "init", "(Ljava/lang/String;S)V");
    assert(file_attrs_init != nullptr);
    if (file_attrs_init == nullptr) throw 2;

    // get the method IDs of the WalkFileTreeVisitor interface
    assert(visitor != nullptr);
    class_name = "spartan.io.Files.WalkFileTreeVisitor";
    auto const visitor_cls = env->GetObjectClass(visitor);
    assert(visitor_cls != nullptr);
    if (visitor_cls == nullptr) throw 1;
    auto const preVisitDirectory = env->GetMethodID(visitor_cls, method_name = "preVisitDirectory",
                                                "(Ljava/nio/file/Path;Ljava/nio/file/attribute/BasicFileAttributes;)I");
    assert(preVisitDirectory != nullptr);
    if (preVisitDirectory == nullptr) throw 2;
    auto const visitFile = env->GetMethodID(visitor_cls, method_name = "visitFile",
                                            "(Ljava/nio/file/Path;Ljava/nio/file/attribute/BasicFileAttributes;)I");
    assert(visitFile != nullptr);
    if (visitFile == nullptr) throw 2;
    auto const visitFileFailed = env->GetMethodID(visitor_cls, method_name = "visitFileFailed",
                                                  "(Ljava/nio/file/Path;Ljava/io/IOException;)I");
    assert(visitFileFailed != nullptr);
    if (visitFileFailed == nullptr) throw 2;
    auto const postVisitDirectory = env->GetMethodID(visitor_cls, method_name = "postVisitDirectory",
                                                     "(Ljava/nio/file/Path;Ljava/io/IOException;)I");
    assert(postVisitDirectory != nullptr);
    if (postVisitDirectory == nullptr) throw 2;

    ff.walk_file_tree(src_dir_sv.c_str(),
          [env, file_path, file_name, path_init, file_attributes, file_attrs_init, visitor,
              preVisitDirectory, visitFile, visitFileFailed, postVisitDirectory]
              (
                  const char *const filepath, const char *const filename, const int depth,
                  const unsigned char d_type, const VisitKind vk) -> VisitResult
          {
            auto const defer_jstr = [env](jstring p)
            {
              if (p != nullptr) {
                env->DeleteLocalRef(p);
              }
            };
            std::unique_ptr<_jstring, decltype(defer_jstr)> sp_filepath{ env->NewStringUTF(filepath), defer_jstr };
            std::unique_ptr<_jstring, decltype(defer_jstr)> sp_filename{ env->NewStringUTF(filename), defer_jstr };

            env->CallVoidMethod(file_name, path_init, sp_filename.get(), nullptr);
            env->CallVoidMethod(file_path, path_init, sp_filepath.get(), file_name);
            env->CallVoidMethod(file_attributes, file_attrs_init, sp_filepath.get(), static_cast<short>(d_type));

            auto rtn = static_cast<int>(VR::CONTINUE);

            switch (d_type) {
              case DT_DIR: {
                switch (vk) {
                  case VK::POST_VISIT_DIRECTORY: {
                    rtn = env->CallIntMethod(visitor, postVisitDirectory, file_path, nullptr);
//                  printf("%20s: %2d %s\n", "POST_VISIT_DIRECTORY", rtn, filepath);
                    break;
                  }
                  case VK::PRE_VISIT_DIRECTORY: {
                    rtn = env->CallIntMethod(visitor, preVisitDirectory, file_path, file_attributes);
//                  printf("%20s: %2d %s\n", "PRE_VISIT_DIRECTORY", rtn, filepath);
                    break;
                  }
                  default:
                    assert(false); // should never reach here
                }
                break;
              }
              case DT_REG:
              case DT_LNK: {
                rtn = env->CallIntMethod(visitor, visitFile, file_path, file_attributes);
//              printf("%20s: %2d %s\n", "VISIT_FILE (reg|lnk)", rtn, filepath);
                break;
              }
              default: {
                rtn = env->CallIntMethod(visitor, visitFile, file_path, file_attributes);
//              printf("%20s: %2d %s\n", "VISIT_FILE (other)", rtn, filepath);
              }
            }

            return static_cast<VisitResult>(rtn);
          });
  } catch(int which) {
    // TODO: need to throw an exception to the Java layer for this case (probably should be a Runtime-derived exception)
    const auto pid = getpid();
    switch (which) {
      case 1:
        log(LL::ERR, R"(process %d: %s() failed finding Java class "%s")", pid,  __func__, class_name);
        break;
      case 2:
        log(LL::ERR, R"(process %d: %s() failed finding Java method "%s" on class "%s")",
            pid, __func__, method_name, class_name);
        break;
      case 3:
        log(LL::ERR, R"(process %d: %s() failed allocating object instance of class "%s")", pid, __func__, class_name);
        break;
      default:
        log(LL::ERR, "process %d: %s() unspecified exception per \"%s::%s()\"", pid, __func__, method_name, class_name);
    }
    _exit(EXIT_FAILURE);
  } catch(const findfiles_exception &ex) {
    // TODO: need to wrap and throw this as a java.io.IOException to the Java layer
    log(LL::ERR, "process %d: spartan.io.Files.%s() %s: %s", getpid(), __func__, ex.name(), ex.what());
    _exit(EXIT_FAILURE);
  } catch(...) {
    // TODO: need to wrap error and throw an exception to the Java layer (probably should be a Runtime-derived exception)
    const auto ex_nm = get_unmangled_name(abi::__cxa_current_exception_type()->name());
    log(LL::ERR, "process %d: spartan.io.Files.%s() terminating due to unhandled exception of type %s",
        getpid(), __func__, ex_nm.c_str());
    _exit(EXIT_FAILURE);
  }
}