/* findfiles.h

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
#ifndef __FINDFILES_H__
#define __FINDFILES_H__

#include <functional>
#include "spartan-exception.h"
#include "path-concat.h"

enum class VisitResult : char { CONTINUE = 0, TERMINATE, SKIP_SUBTREE, SKIP_SIBLINGS };
using VR = VisitResult;

enum class VisitKind : char { POST_VISIT_DIRECTORY = 1, PRE_VISIT_DIRECTORY, VISIT_FILE, VISIT_FILE_FAILED };
using VK = VisitKind;

using findfiles_ex_cb_t = std::function<VisitResult(const char *filepath, const char *filename, int depth,
                                                    unsigned char d_type, VisitKind vk)>;

class find_files {
public:
  static const int MAXDEPTH = 0x7FFFFFFF;
private:
  const char separator_char = kPathSeparator; // defaults to POSIX separator character
  const int maxdepth = MAXDEPTH;              // implies essentially unlimited tree depth walking
  const bool follow_links = false;            // default behavior is to not follow links (identify them as type DT_LNK)
  // for debug/trace diagnostic use
  bool do_print_using_stat = true;
  bool do_print_not_using_stat = true;
public:
  find_files() = default;
  explicit find_files(char separator_char) noexcept : separator_char(separator_char) {}
  int normalize_depth(int depth) { return depth <= 0 ? 1 : depth; }
  explicit find_files(int maxdepth) noexcept : maxdepth(normalize_depth(maxdepth)) {}
  explicit find_files(bool follow_links) noexcept : follow_links(follow_links) {}
  explicit find_files(char separator_char, int maxdepth) noexcept : separator_char(separator_char),
                                                                    maxdepth(normalize_depth(maxdepth)) {}
  explicit find_files(char separator_char, bool follow_links) noexcept : separator_char(separator_char),
                                                                         follow_links(follow_links) {}
  explicit find_files(char separator_char, int maxdepth, bool follow_links) noexcept : separator_char(separator_char),
                                                                                       maxdepth(
                                                                                           normalize_depth(maxdepth)),
                                                                                       follow_links(follow_links) {}
  find_files(const find_files&) noexcept = default;
  find_files(const find_files&&) noexcept = delete;
  find_files& operator=(const find_files&) noexcept = delete;
  find_files& operator=(const find_files&&) noexcept = delete;
  ~find_files() = default;
private: // helper methods
  bool walk_file_tree(int depth, const char *start_dir, findfiles_ex_cb_t &callback);
  bool visit_dir(int depth, int &skip_sibs, const char *filepath, const char *filename, findfiles_ex_cb_t &callback);
  bool visit_file(int depth, int &skip_sibs, const char *filepath, const char *filename, unsigned char d_type,
                  findfiles_ex_cb_t &callback);
public: // core API methods
  char get_separator_char() const { return separator_char; }
  bool walk_file_tree(const char *start_dir, findfiles_ex_cb_t callback);
};

using findfiles_cb_t = std::function<bool(const char *filepath, const char *filename)>;

// backward compatibility API function
bool findfiles(const char *start_dir, findfiles_cb_t callback);

// declare findfiles_exception
DECL_EXCEPTION(findfiles)

#endif // __FINDFILES_H__