/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_TOOLS_MCTK_DEBUG_H_
#define CAMERA_TOOLS_MCTK_DEBUG_H_

#include <stdlib.h>

#include <source_location>
#include <string> /* to_string */
#include <string_view>

/* The function names here are UPPER CASE in order to be consistent with
 * their classic use as macros, and with the one remaining macro MCTK_ASSERT.
 */

/* Print a message, without a prefix. */
void MCTK_VERBOSE(std::string_view msg);

/* Print a message, prefixed with the caller's name. */
void MCTK_ERR(std::string_view msg,
              std::source_location = std::source_location::current());

void MCTK_PERROR(std::string_view msg,
                 std::source_location = std::source_location::current());

/* Print failed expression, then halt and catch fire. */
#define MCTK_ASSERT(exp)                      \
  do {                                        \
    if (!(exp)) {                             \
      MCTK_ERR("Failed assertion on: " #exp); \
                                              \
      exit(EXIT_FAILURE);                     \
    }                                         \
  } while (0)

/* Print both expressions of a failed equality, then halt and catch fire. */
// clang-format off
#define MCTK_ASSERT_EQ(exp1, exp2)                                           \
  do {                                                                       \
    if (!((exp1) == (exp2))) {                                               \
      MCTK_ERR("Failed assertion on: MCTK_ASSERT_EQ(" #exp1 ", " #exp2 ")"); \
      MCTK_ERR("Values             : " +                                     \
               std::to_string(exp1) + " == " + std::to_string(exp2));        \
                                                                             \
      exit(EXIT_FAILURE);                                                    \
    }                                                                        \
  } while (0)
// clang-format on

/* Print error message, then halt and catch fire. */
[[noreturn]] void MCTK_PANIC(
    std::string_view msg,
    std::source_location = std::source_location::current());

#endif /* CAMERA_TOOLS_MCTK_DEBUG_H_ */
