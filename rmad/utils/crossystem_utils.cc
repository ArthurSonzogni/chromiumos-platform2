// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/crossystem_utils.h"

namespace rmad {

const char kHwwpStatus[] = "wpsw_cur";

int CrosSystemUtils::GetHwwpStatus() const {
  int ret = -1;
  GetInt(kHwwpStatus, &ret);
  return ret;
}

}  // namespace rmad
