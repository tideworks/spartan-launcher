/* findfiles.cpp

Copyright 2015, 2018 Tideworks Technology
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
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cstring>
#include <cerrno>
#include <alloca.h>
#include "log.h"
#include "format2str.h"
#include "string-view.h"
#include "findfiles.h"

using logger::log;
using logger::LL;

using bpstd::string_view;

// core API method
bool find_files::walk_file_tree(const char * const start_dir, findfiles_ex_cb_t callback) {
  char * const start_dir_dup = strdupa(start_dir);
  callback(start_dir, basename(start_dir_dup), 0, DT_DIR, VK::PRE_VISIT_DIRECTORY);
  return walk_file_tree(1, start_dir, callback);
}

bool find_files::walk_file_tree(const int depth, const char * const start_dir, findfiles_ex_cb_t &callback) {
  const char * const func = __func__;
  const string_view startdir_sv{start_dir};
  DIR * const d = opendir(startdir_sv.c_str());
  if (d == nullptr) {
    const char * const err_msg_fmt = "could not open specified directory \"%s\":\n\t%s";
    auto errmsg( format2str(err_msg_fmt, startdir_sv.c_str(), strerror(errno)) );
    throw findfiles_exception(std::move(errmsg));
  }
  auto const close_dir = [func, start_dir](DIR *pd) {
    if (pd != nullptr) {
      closedir(pd);
#if defined(TEST_FINDFILES)
      log(LL::TRACE, "closed DIR for \"%s\"", func, start_dir);
#endif
    }
  };
  std::unique_ptr<DIR, decltype(close_dir)> dir_sp(d, close_dir);

  auto const print_using_stat = [this]() -> void {
    if (do_print_using_stat) {
      do_print_using_stat = false;
      log(LL::DEBUG, "using stat()");
    }
  };
  auto const print_not_using_stat = [this]() -> void {
    if (do_print_not_using_stat) {
      do_print_not_using_stat = false;
      log(LL::DEBUG, "not using stat()");
    }
  };

  const size_t strbuf_size = 2048;
  auto const strbuf = static_cast<char*>(alloca(strbuf_size));
  struct stat statbuf{0};
  struct dirent *dir = nullptr;
  auto last_ch = startdir_sv.back();
  const auto no_explicit_sep_ch = last_ch == kPathSeparator || last_ch == separator_char;

  int skip_sibs = 0;
  bool stop = false;

  while (!stop && (dir = readdir(d)) != nullptr) {
    if (depth == skip_sibs || strcmp(".", dir->d_name) == 0 || strcmp("..", dir->d_name) == 0) continue;

    int n = 0;
    if (no_explicit_sep_ch) {
      n = snprintf(strbuf, strbuf_size, "%s%s", startdir_sv.c_str(), dir->d_name);
    } else {
      n = snprintf(strbuf, strbuf_size, "%s%c%s", startdir_sv.c_str(), separator_char, dir->d_name);
    }
    if (n <= 0 || static_cast<unsigned>(n) >= strbuf_size) {
      const char * const err_msg_fmt = "failed forming full path name for \"%s\"";
      throw findfiles_exception( format2str(err_msg_fmt, dir->d_name) );
    }

    const string_view strbuf_sv{strbuf};

    if (dir->d_type == DT_UNKNOWN || (dir->d_type == DT_LNK && follow_links)) {
      print_using_stat();
      if (stat(strbuf_sv.c_str(), &statbuf) == -1) {
        [n, strbuf]() -> void {
          const char *const pathname = strndupa(strbuf, static_cast<size_t >(n));
          const char err_msg_fmt[] = "stat() failed on \"%s\"\n\t%s";
          auto errmsg(format2str(err_msg_fmt, pathname, strerror(errno)));
          log(LL::TRACE, errmsg.c_str());
        }();
        auto vr = callback(strbuf_sv.c_str(), dir->d_name, depth, dir->d_type, VK::VISIT_FILE_FAILED);
        switch (vr) {
          case VR::TERMINATE:
            stop = true;
            break;
          case VR::SKIP_SIBLINGS:
            skip_sibs = depth;
            break;
          default:
            break;
        }
//        throw findfiles_exception(std::move(errmsg));
      } else {
        switch (statbuf.st_mode & S_IFMT) {
          case S_IFDIR: {
            if (depth < maxdepth) {
              stop = visit_dir(depth, skip_sibs, strbuf_sv.c_str(), dir->d_name, callback);
            }
            break;
          }
          case S_IFREG: {
            stop = visit_file(depth, skip_sibs, strbuf_sv.c_str(), dir->d_name, DT_REG, callback);
            break;
          }
          default:
            continue;
        }
      }
    } else {
      print_not_using_stat();
      if (dir->d_type == DT_DIR) {
        if (depth < maxdepth) {
          stop = visit_dir(depth, skip_sibs, strbuf_sv.c_str(), dir->d_name, callback);
        }
      } else {
        stop = visit_file(depth, skip_sibs, strbuf_sv.c_str(), dir->d_name, dir->d_type, callback);
      }
    }
  }
  return stop;
}

bool find_files::visit_dir(const int depth, int &skip_sibs, const char *const filepath, const char *const filename,
                           findfiles_ex_cb_t &callback)
{
  bool stop = false;
  auto vr = callback(filepath, filename, depth, DT_DIR, VK::PRE_VISIT_DIRECTORY);
  switch (vr) {
    case VR::TERMINATE:
      stop = true;
      break;
    case VR::SKIP_SUBTREE:
      break;
    case VR::SKIP_SIBLINGS:
      skip_sibs = depth;
      break;
    default: {
      stop = walk_file_tree(depth + 1, filepath, callback);
      vr = callback(filepath, filename, depth, DT_DIR, VK::POST_VISIT_DIRECTORY);
      switch (vr) {
        case VR::TERMINATE:
          stop = true;
          break;

        default:
          break;
      }
    }
  }
  return stop;
}

bool find_files::visit_file(const int depth, int &skip_sibs, const char *const filepath, const char *const filename,
                            unsigned char d_type, findfiles_ex_cb_t &callback)
{
  bool stop = false;
  auto vr = callback(filepath, filename, depth, d_type, VK::VISIT_FILE);
  switch (vr) {
    case VR::TERMINATE:
      stop = true;
      break;
    case VR::SKIP_SIBLINGS:
      skip_sibs = depth;
      break;
    default:
      break;
  }
  return stop;
}

// backward compatibility API function
bool findfiles(const char * const start_dir, findfiles_cb_t callback) {
  static const char * const func = __func__;
  const bool follow_links = true;
  find_files ff{follow_links};
  return ff.walk_file_tree(start_dir, [&callback](const char *const filepath, const char *const filename,
                                                  const int depth, const unsigned char d_type,
                                                  const VisitKind vk) -> VisitResult
  {
    switch (d_type) {
      case DT_DIR:
        return VR::CONTINUE;
      case DT_REG:
        return callback(filepath, filename) ? VR::TERMINATE : VR::CONTINUE;
      default: {
        const char err_msg_fmt[] = "%s() unexpected file tree node type %c for \"%s\"";
        throw findfiles_exception(format2str(err_msg_fmt, func, d_type, filepath));
      }
    }
  });
}

#if defined(TEST_FINDFILES)
int main(int argc, char **argv) {
  const char * const progname = argv[0];
  const char * const start_dir = argc > 1 ? argv[1] : ".";
  const char * const match_name = argc > 2 ? argv[2] : nullptr;
  logger::set_progname(progname);
  logger::set_level(LL::DEBUG);
  log(LL::INFO, "%s: search starting at: \"%s\":\n", progname, start_dir);
  // exhaustively list all files starting at specified directory
  try {
    if (!findfiles(start_dir, [progname,match_name](const char * const filepath, const char * const filename) {
      log(LL::TRACE, "\"%s\" : \"%s\"", filepath, filename);
      if (match_name != nullptr && strcmp(filename, match_name) == 0) {
        log(LL::INFO, "found file (stopping search) at path:\n\t\"%s\"", filepath);
        return true;
      }
      return false;
    })) {
      log(LL::INFO, "file \"%s\" not found", match_name);
    }
  } catch(findfiles_exception& ex) {
    log(LL::ERR, "%s: %s", ex.name(), ex.what());
    return 1;
  }
  return 0;
}
#endif
