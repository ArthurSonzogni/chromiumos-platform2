/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_TOOLS_MCTK_DEBUG_H_
#define CAMERA_TOOLS_MCTK_DEBUG_H_

#include <assert.h>
#include <errno.h>

#include <iostream>
#include <string>

#define MCTK_ASSERT(exp) \
  do {                   \
    assert(exp);         \
  } while (0)

#define MCTK_INFO(msg)             \
  do {                             \
    std::cerr << msg << std::endl; \
  } while (0)

#define MCTK_ERR(msg)                                               \
  do {                                                              \
    std::cerr << std::string(__func__) << ": " << msg << std::endl; \
  } while (0)

#define MCTK_PERROR(msg)                  \
  do {                                    \
    std::string temp = std::string(msg);  \
    temp += ": ";                         \
    temp += std::string(strerror(errno)); \
                                          \
    MCTK_ERR(temp);                       \
  } while (0)

/* Print error, then halt and catch fire.
 * If assert(0) works, then this function never returns.
 */
#define MCTK_PANIC(msg) \
  do {                  \
    MCTK_ERR(msg);      \
    assert(0);          \
  } while (0)

#endif /* CAMERA_TOOLS_MCTK_DEBUG_H_ */
