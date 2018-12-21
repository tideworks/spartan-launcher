/* so-export.h

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
#ifndef __SO_EXPORT_H__
#define __SO_EXPORT_H__


#ifndef __has_attribute
  #define __has_attribute(x) 0
#endif
#if (defined(__GNUC__) && ((__GNUC__ > 4) || (__GNUC__ == 4) && (__GNUC_MINOR__ > 2))) || __has_attribute(visibility)
  #define SO_EXPORT     __attribute__((visibility("default")))
  #define SO_EXPORT     __attribute__((visibility("default")))
#else
  #define SO_EXPORT
  #define SO_EXPORT
#endif

extern "C" SO_EXPORT int one_time_init_main(int argc, char **argv);
extern "C" SO_EXPORT int forkable_main_entry(int argc, char **argv, const bool is_extended_invoke);

#endif /* __SO_EXPORT_H__ */