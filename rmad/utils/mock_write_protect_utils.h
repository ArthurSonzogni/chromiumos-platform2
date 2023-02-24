// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_MOCK_WRITE_PROTECT_UTILS_H_
#define RMAD_UTILS_MOCK_WRITE_PROTECT_UTILS_H_

#include "rmad/utils/write_protect_utils.h"

namespace rmad {

class MockWriteProtectUtils : public WriteProtectUtils {
 public:
  MockWriteProtectUtils() = default;
  ~MockWriteProtectUtils() override = default;
};

}  // namespace rmad

#endif  // RMAD_UTILS_MOCK_WRITE_PROTECT_UTILS_H_
