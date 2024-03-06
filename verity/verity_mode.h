// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VERITY_VERITY_MODE_H_
#define VERITY_VERITY_MODE_H_

#include <string>

#include <brillo/brillo_export.h>

namespace verity {

constexpr char kVerityModeCreate[] = "create";
constexpr char kVerityModeVerify[] = "verify";

typedef enum { VERITY_NONE = 0, VERITY_CREATE, VERITY_VERIFY } verity_mode_t;

BRILLO_EXPORT verity_mode_t ToVerityMode(const std::string& verity_mode_str);

}  // namespace verity

#endif  // VERITY_VERITY_MODE_H_
