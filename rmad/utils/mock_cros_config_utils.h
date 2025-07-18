// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_MOCK_CROS_CONFIG_UTILS_H_
#define RMAD_UTILS_MOCK_CROS_CONFIG_UTILS_H_

#include <string>
#include <vector>

#include <gmock/gmock.h>

#include "rmad/utils/cros_config_utils.h"

namespace rmad {

class MockCrosConfigUtils : public CrosConfigUtils {
 public:
  MockCrosConfigUtils() = default;
  ~MockCrosConfigUtils() override = default;

  MOCK_METHOD(bool, GetRmadCrosConfig, (RmadCrosConfig*), (const, override));
  MOCK_METHOD(bool, GetModelName, (std::string*), (const, override));
  MOCK_METHOD(bool, GetBrandCode, (std::string*), (const, override));
  MOCK_METHOD(bool, GetSkuId, (uint32_t*), (const, override));
  MOCK_METHOD(bool, GetCustomLabelTag, (std::string*), (const, override));
  MOCK_METHOD(bool, GetFirmwareConfig, (uint32_t*), (const, override));
  MOCK_METHOD(std::optional<std::string>,
              GetSpiFlashTransform,
              (const std::string&),
              (const, override));
  MOCK_METHOD(std::optional<std::string>,
              GetFingerprintSensorLocation,
              (),
              (const, override));
  MOCK_METHOD(std::optional<std::string>, GetOemName, (), (const, override));
  MOCK_METHOD(bool,
              GetDesignConfigList,
              (std::vector<DesignConfig>*),
              (const override));
  MOCK_METHOD(bool, GetSkuIdList, (std::vector<uint32_t>*), (const override));
  MOCK_METHOD(bool,
              GetCustomLabelTagList,
              (std::vector<std::string>*),
              (const override));
  MOCK_METHOD(std::optional<std::string>,
              GetSoundCardConfig,
              (),
              (const, override));
  MOCK_METHOD(std::optional<std::string>, GetSpeakerAmp, (), (const, override));
};

}  // namespace rmad

#endif  // RMAD_UTILS_MOCK_CROS_CONFIG_UTILS_H_
