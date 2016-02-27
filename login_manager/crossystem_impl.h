// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_CROSSYSTEM_IMPL_H_
#define LOGIN_MANAGER_CROSSYSTEM_IMPL_H_

#include "login_manager/crossystem.h"

class CrossystemImpl : public Crossystem {
 public:
  // Reads a system property integer.
  int VbGetSystemPropertyInt(const char* name);

  // Sets a system property integer.
  int VbSetSystemPropertyInt(const char* name, int value);

  // Reads a system property string into a destination buffer.
  const char* VbGetSystemPropertyString(const char* name,
                                        char* dest,
                                        std::size_t size);

  // Sets a system property string.
  int VbSetSystemPropertyString(const char* name, const char* value);
};

#endif  // LOGIN_MANAGER_CROSSYSTEM_IMPL_H_
