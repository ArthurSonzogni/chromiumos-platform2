// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_MOCK_GSC_UTILS_H_
#define RMAD_UTILS_MOCK_GSC_UTILS_H_

#include <cstdint>
#include <optional>
#include <string>

#include <gmock/gmock.h>

#include "rmad/utils/gsc_utils.h"

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
  MOCK_METHOD(bool, IsInitialFactoryModeEnabled, (), (const, override));
  MOCK_METHOD(std::optional<std::string>,
              GetBoardIdType,
              (),
              (const, override));
  MOCK_METHOD(std::optional<std::string>,
              GetBoardIdFlags,
              (),
              (const, override));
  MOCK_METHOD(bool, SetBoardId, (bool), (const, override));
  MOCK_METHOD(bool, Reboot, (), (const, override));
  MOCK_METHOD(std::optional<FactoryConfig>,
              GetFactoryConfig,
              (),
              (const, override));
  MOCK_METHOD(bool,
              SetFactoryConfig,
              (bool is_chassis_branded, int hw_compliance_version),
              (const, override));
  MOCK_METHOD(std::optional<bool>, GetChassisOpenStatus, (), (override));
  MOCK_METHOD(SpiAddressingMode, GetAddressingMode, (), (override));
  MOCK_METHOD(bool, SetAddressingMode, (SpiAddressingMode mode), (override));
  MOCK_METHOD(SpiAddressingMode,
              GetAddressingModeByFlashSize,
              (uint64_t flash_size),
              (override));
  MOCK_METHOD(bool, SetWpsr, (std::string_view), (override));
  MOCK_METHOD(std::optional<bool>, IsApWpsrProvisioned, (), (override));
};

}  // namespace rmad

#endif  // RMAD_UTILS_MOCK_GSC_UTILS_H_
