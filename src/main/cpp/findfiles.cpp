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
#include <memory>
#include "log.h"
#include "path-concat.h"
#include "format2str.h"
#include "findfiles.h"

using namespace logger;

bool findfiles(const char * const wrkdir, const findfiles_cb_t &callback) {
  DIR * const d = opendir(wrkdir);
  if (d == nullptr) {
    const char * const err_msg_fmt = "could not open specified directory \"%s\":\n\t%s";
    auto errmsg( format2str(err_msg_fmt, wrkdir, strerror(errno)) );
    throw findfiles_exception(std::move(errmsg));
  }
  const char * const func = __func__;
  auto const close_dir = [wrkdir,func](DIR *pd) {
    if (pd != nullptr) {
      closedir(pd);
#if defined(TEST_FINDFILES)
      log(LL::TRACE, "closed DIR for \"%s\"", func, wrkdir);
#endif
    }
  };
  std::unique_ptr<DIR, decltype(close_dir)> dir_sp(d, close_dir);

  const size_t strbuf_size = 1024;
  auto const strbuf = (char*) alloca(strbuf_size);

  struct stat statbuf{};
  struct dirent *dir;
  bool stop = false;
  while (!stop && (dir = readdir(d)) != nullptr) {
    if (strcmp(".", dir->d_name) == 0 || strcmp("..", dir->d_name) == 0) continue;

    int n = snprintf(strbuf, strbuf_size, "%s%c%s", wrkdir, kPathSeparator, dir->d_name);
    if (n <= 0 || static_cast<unsigned>(n) >= strbuf_size) {
      const char * const err_msg_fmt = "failed forming path name for \"%s\"";
      auto errmsg( format2str(err_msg_fmt, dir->d_name) );
      throw findfiles_exception(std::move(errmsg));
    }

    if (stat(strbuf, &statbuf) == -1) {
      const char * const pathname = strndupa(strbuf, static_cast<size_t >(n));
      const char * const err_msg_fmt = "stat() failed on \"%s\"\n\t%s";
      auto errmsg( format2str(err_msg_fmt, pathname, strerror(errno)) );
      log(LL::TRACE, errmsg.c_str());
      continue;
//    throw findfiles_exception(std::move(errmsg));
    }

    switch (statbuf.st_mode & S_IFMT) {
      case S_IFDIR:
        stop = findfiles(strbuf, callback);
        break;
      case S_IFREG:
        stop = callback(strbuf, dir->d_name);
        break;
      default:
        continue;
    }
  }
  return stop;
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
