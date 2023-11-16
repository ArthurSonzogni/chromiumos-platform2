// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_MOCK_HWID_UTILS_H_
#define RMAD_UTILS_MOCK_HWID_UTILS_H_

#include "rmad/utils/hwid_utils.h"

#include <string>

#include <gmock/gmock.h>

namespace rmad {

class MockHwidUtils : public HwidUtils {
 public:
  MockHwidUtils() = default;
  ~MockHwidUtils() override = default;

  MOCK_METHOD(bool, VerifyChecksum, (const std::string&), (override));
  MOCK_METHOD(bool,
              VerifyHwidFormat,
              (const std::string&, bool has_checksum),
              (override));
  MOCK_METHOD(std::optional<HwidElements>,
              DecomposeHwid,
              (const std::string&),
              (override));
  MOCK_METHOD(std::optional<std::string>,
              CalculateChecksum,
              (const std::string&),
              (const, override));
};

}  // namespace rmad

#endif  // RMAD_UTILS_MOCK_HWID_UTILS_H_
