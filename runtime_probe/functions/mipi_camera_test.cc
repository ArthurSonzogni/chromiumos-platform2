// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "cros-camera/device_config.h"
#include "runtime_probe/functions/mipi_camera.h"
#include "runtime_probe/utils/function_test_utils.h"

namespace runtime_probe {
namespace {

class FakeMipiCameraFunction : public MipiCameraFunction {
  using MipiCameraFunction::MipiCameraFunction;

  std::optional<std::vector<cros::PlatformCameraInfo>> GetPlatformCameraInfo()
      const override {
    return fake_cameras_;
  }

 public:
  // The fake cameras used by fake function.
  std::optional<std::vector<cros::PlatformCameraInfo>> fake_cameras_;
};

class MipiCameraFunctionTest : public BaseFunctionTest {};

cros::PlatformCameraInfo CreateEepromPlatformCameraInfo(std::string sysfs_name,
                                                        std::string module_vid,
                                                        uint16_t module_pid,
                                                        std::string sensor_vid,
                                                        uint16_t sensor_pid) {
  cros::EepromIdBlock id_block = {
      .module_pid = module_pid,
      .module_vid{'A', 'A'},
      .sensor_vid{'A', 'A'},
      .sensor_pid = sensor_pid,
  };
  cros::PlatformCameraInfo camera_info = {
      .eeprom = cros::EepromInfo{.id_block = std::move(id_block)},
      .sysfs_name = sysfs_name,
  };
  return camera_info;
}

cros::PlatformCameraInfo CreateV4L2PlatformCameraInfo(std::string name,
                                                      std::string vendor_id) {
  cros::V4L2SensorInfo v4l2_sensor = {.name = name, .vendor_id = vendor_id};
  cros::PlatformCameraInfo camera_info = {.v4l2_sensor =
                                              std::move(v4l2_sensor)};
  return camera_info;
}

TEST_F(MipiCameraFunctionTest, ProbeMipiCamera) {
  const base::Value probe_statement(base::Value::Type::DICTIONARY);
  auto probe_function =
      CreateProbeFunction<FakeMipiCameraFunction>(probe_statement);

  probe_function->fake_cameras_ = std::vector<cros::PlatformCameraInfo>{
      CreateEepromPlatformCameraInfo("ABC-00/ABC-1234", "TC", 1234u, "OV",
                                     4321u),
      CreateV4L2PlatformCameraInfo("AAAA", "BBBB"),
  };

  auto result = probe_function->Eval();
  auto ans = CreateProbeResultFromJson(R"JSON(
    [{
      "module_id": "AA04d2",
      "name": "ABC-00/ABC-1234",
      "sensor_id": "AA10e1"
    },
    {
      "name": "AAAA",
      "vendor": "BBBB"
    }]
  )JSON");
  EXPECT_EQ(result, ans);
}

TEST_F(MipiCameraFunctionTest, GetDeviceConfigFailed) {
  const base::Value probe_statement(base::Value::Type::DICTIONARY);
  auto probe_function =
      CreateProbeFunction<FakeMipiCameraFunction>(probe_statement);

  // Fail to get device config.
  probe_function->fake_cameras_ = std::nullopt;

  auto result = probe_function->Eval();
  auto ans = CreateProbeResultFromJson(R"JSON(
    []
  )JSON");
  EXPECT_EQ(result, ans);
}

TEST_F(MipiCameraFunctionTest, NoCamera) {
  const base::Value probe_statement(base::Value::Type::DICTIONARY);
  auto probe_function =
      CreateProbeFunction<FakeMipiCameraFunction>(probe_statement);

  // Get empty camera list.
  probe_function->fake_cameras_ = std::vector<cros::PlatformCameraInfo>{};

  auto result = probe_function->Eval();
  auto ans = CreateProbeResultFromJson(R"JSON(
    []
  )JSON");
  EXPECT_EQ(result, ans);
}

}  // namespace
}  // namespace runtime_probe
