/* cfgparse.h

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
#ifndef __CFGPARSE_H__
#define __CFGPARSE_H__

#include "spartan-exception.h"
#include "ini.h"

// declare process_cfg_exception
DECL_EXCEPTION(process_cfg)

bool process_config(const char * const dirpath, const char * const cfgfilename, const cfg_parse_handler_t &handler);

#endif // __CFGPARSE_H__
