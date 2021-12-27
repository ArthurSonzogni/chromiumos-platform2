// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_MOCK_SSFC_UTILS_H_
#define RMAD_UTILS_MOCK_SSFC_UTILS_H_

#include "rmad/utils/ssfc_utils.h"

#include <string>

#include <gmock/gmock.h>

namespace rmad {

class MockSsfcUtils : public SsfcUtils {
 public:
  MockSsfcUtils() = default;
  ~MockSsfcUtils() override = default;

  MOCK_METHOD(bool,
              GetSSFC,
              (const std::string&, bool*, uint32_t*),
              (const, override));
};

}  // namespace rmad

#endif  // RMAD_UTILS_MOCK_SSFC_UTILS_H_
