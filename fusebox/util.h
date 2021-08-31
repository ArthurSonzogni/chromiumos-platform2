// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FUSEBOX_UTIL_H_
#define FUSEBOX_UTIL_H_

#include <string>

// Returns error code string with an optional prefix.
std::string ErrorToString(int error, const std::string& prefix = {});

#endif  // FUSEBOX_UTIL_H_
