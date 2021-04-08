// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/utils.h"

#include <string>

namespace biod {

std::string LogSafeID(const std::string& id) {
  // Truncate the string to the first 2 chars without extending to 2 chars.
  if (id.length() > 2) {
    return id.substr(0, 2) + "*";
  }
  return id;
}

}  // namespace biod
