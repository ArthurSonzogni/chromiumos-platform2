// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_SOMMELIER_SOMMELIER_UTIL_H_
#define VM_TOOLS_SOMMELIER_SOMMELIER_UTIL_H_

#include <assert.h>
#include <cerrno>
#include <cstring>
#include <memory>

#include <wayland-server.h>

#define errno_assert(rv)                                          \
  {                                                               \
    int macro_private_assert_value = (rv);                        \
    if (!macro_private_assert_value) {                            \
      fprintf(stderr, "Unexpected error: %s\n", strerror(errno)); \
      assert(false);                                              \
    }                                                             \
  }

#define UNUSED(x) ((void)(x))

// Performs an asprintf operation and checks the result for validity and calls
// abort() if there's a failure. Returns a newly allocated string rather than
// taking a double pointer argument like asprintf.
__attribute__((__format__(__printf__, 1, 0))) char* sl_xasprintf(
    const char* fmt, ...);

#define DEFAULT_DELETER_FDECL(TypeName) \
  namespace std {                       \
  template <>                           \
  struct default_delete<TypeName> {     \
    void operator()(TypeName* ptr);     \
  };                                    \
  }

DEFAULT_DELETER_FDECL(struct wl_event_source);

#endif  // VM_TOOLS_SOMMELIER_SOMMELIER_UTIL_H_
