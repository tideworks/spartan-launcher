/* launch-program.h

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
#ifndef __LAUNCH_PROGRAM_H__
#define __LAUNCH_PROGRAM_H__

#include "so-export.h"

namespace launch_program {
  // NOTE: this property must be set prior to using Java_spartan_LaunchProgram_invokeCommand()
  SO_EXPORT void set_progpath(const char *const progpath);
  SO_EXPORT std::tuple<std::string,bool> try_resolve_program_path(const char * const prog,
                                                                  const char * const path_var_name);
}

#endif //__LAUNCH_PROGRAM_H__
