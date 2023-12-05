/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "tools/mctk/debug.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <iostream>
#include <source_location>
#include <string_view>

/* Print a message, without a prefix. */
void MCTK_INFO(std::string_view msg) {
  std::cerr << msg << std::endl;
}

/* Print a message, prefixed with the caller's name. */
void MCTK_ERR(std::string_view msg, const std::source_location sl) {
  std::cerr << sl.function_name() << ": " << msg << std::endl;
}

void MCTK_PERROR(std::string_view msg, const std::source_location sl) {
  std::cerr << sl.function_name() << ": " << msg << ": " << strerror(errno)
            << std::endl;
}

/* Print error message, then halt and catch fire. */
[[noreturn]] void MCTK_PANIC(std::string_view msg,
                             const std::source_location sl) {
  std::cerr << sl.function_name() << ": " << msg << std::endl;

  exit(EXIT_FAILURE);
}
