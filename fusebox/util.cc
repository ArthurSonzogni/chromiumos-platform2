// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fusebox/util.h"

#include <base/strings/stringprintf.h>

std::string ErrorToString(int error, const std::string& prefix) {
  if (!prefix.empty())
    return base::StringPrintf("%s [%d]", prefix.c_str(), error);
  return base::StringPrintf("[%d]", error);
}
