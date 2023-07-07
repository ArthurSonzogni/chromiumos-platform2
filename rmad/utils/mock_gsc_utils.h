// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_MOCK_GSC_UTILS_H_
#define RMAD_UTILS_MOCK_GSC_UTILS_H_

#include "rmad/utils/gsc_utils.h"

#include <string>

#include <gmock/gmock.h>

namespace rmad {

class MockGscUtils : public GscUtils {
 public:
  MockGscUtils() = default;
  ~MockGscUtils() override = default;

  MOCK_METHOD(bool, GetRsuChallengeCode, (std::string*), (const, override));
  MOCK_METHOD(bool, PerformRsu, (const std::string&), (const, override));
  MOCK_METHOD(bool, EnableFactoryMode, (), (const, override));
  MOCK_METHOD(bool, DisableFactoryMode, (), (const, override));
  MOCK_METHOD(bool, IsFactoryModeEnabled, (), (const, override));
  MOCK_METHOD(bool, GetBoardIdType, (std::string*), (const, override));
  MOCK_METHOD(bool, GetBoardIdFlags, (std::string*), (const, override));
  MOCK_METHOD(bool, SetBoardId, (bool), (const, override));
  MOCK_METHOD(bool, Reboot, (), (const, override));
  MOCK_METHOD(bool,
              GetFactoryConfig,
              (bool* is_chassis_branded, int* hw_compliance_version),
              (const, override));
  MOCK_METHOD(bool,
              SetFactoryConfig,
              (bool is_chassis_branded, int hw_compliance_version),
              (const, override));
};

}  // namespace rmad

#endif  // RMAD_UTILS_MOCK_GSC_UTILS_H_
