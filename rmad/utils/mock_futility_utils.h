// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_MOCK_FUTILITY_UTILS_H_
#define RMAD_UTILS_MOCK_FUTILITY_UTILS_H_

#include "rmad/utils/futility_utils.h"

#include <cstdint>
#include <optional>
#include <string>

#include <gmock/gmock.h>

namespace rmad {

class MockFutilityUtils : public FutilityUtils {
 public:
  MockFutilityUtils() = default;
  ~MockFutilityUtils() override = default;

  MOCK_METHOD(std::optional<bool>, GetApWriteProtectionStatus, (), (override));
  MOCK_METHOD(bool, EnableApSoftwareWriteProtection, (), (override));
  MOCK_METHOD(bool, DisableApSoftwareWriteProtection, (), (override));
  MOCK_METHOD(bool, SetHwid, (const std::string&), (override));
  MOCK_METHOD(std::optional<uint64_t>, GetFlashSize, (), (override));
  MOCK_METHOD(std::optional<FlashInfo>, GetFlashInfo, (), (override));
};

}  // namespace rmad

#endif  // RMAD_UTILS_MOCK_FUTILITY_UTILS_H_
