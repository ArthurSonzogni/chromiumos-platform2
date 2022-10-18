// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "runtime_probe/functions/generic_battery.h"
#include "runtime_probe/probe_function.h"
#include "runtime_probe/utils/file_test_utils.h"
#include "runtime_probe/utils/function_test_utils.h"

namespace runtime_probe {
namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;

constexpr auto kEctoolBatteryOutput =
    "Battery info:\n"
    "  OEM name:               123-ABCDEF\n"
    "  Model number:           XYZ-00000-ABC\n"
    "  Chemistry   :           LiP\n"
    "  Serial number:          00C4\n"
    "  Design capacity:        3920 mAh\n";

class GenericBatteryTest : public BaseFunctionTest {
 public:
  GenericBatteryTest() {
    SetFile(bat0_path.Append("manufacturer"), "123-ABC");
    SetFile(bat0_path.Append("model_name"), "XYZ-00000");
    SetFile(bat0_path.Append("technology"), "Li-poly");
    SetFile(bat0_path.Append("type"), "Battery");

    // Missing battery sysfs file.
    SetFile(bat1_path.Append("manufacturer"), "123-ABC");
    SetFile(bat1_path.Append("technology"), "Li-poly");
    SetFile(bat1_path.Append("type"), "Battery");

    // Mismatch battery type "USB".
    SetFile(charger_path.Append("manufacturer"), "123-ABC");
    SetFile(charger_path.Append("model_name"), "XYZ-12345");
    SetFile(charger_path.Append("technology"), "Li-poly");
    SetFile(charger_path.Append("type"), "USB");
  }

 protected:
  const base::FilePath bat0_path{"sys/class/power_supply/BAT0"};
  const base::FilePath bat1_path{"sys/class/power_supply/BAT1"};
  const base::FilePath charger_path{"sys/class/power_supply/CHARGER0"};
};

TEST_F(GenericBatteryTest, Succeed) {
  auto debugd = mock_context()->mock_debugd_proxy();
  EXPECT_CALL(*debugd, BatteryFirmware("info", _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(kEctoolBatteryOutput), Return(true)));
  auto ans = CreateProbeResultFromJson(R"JSON(
    [
      {
        "charge_full_design": "3920000",
        "chemistry": "LiP",
        "manufacturer": "123-ABCDEF",
        "model_name": "XYZ-00000-ABC",
        "technology": "Li-poly",
        "type": "Battery"
      }
    ]
  )JSON");
  ans[0].GetDict().Set("path", GetPathUnderRoot(bat0_path).value());

  base::Value probe_statement(base::Value::Type::DICTIONARY);
  auto probe_function = CreateProbeFunction<GenericBattery>(probe_statement);
  auto result = probe_function->Eval();
  EXPECT_EQ(result, ans);
}

TEST_F(GenericBatteryTest, CallEctoolFailed) {
  auto debugd = mock_context()->mock_debugd_proxy();
  EXPECT_CALL(*debugd, BatteryFirmware("info", _, _, _))
      .WillOnce(DoAll(Return(false)));
  // "chemistry" and "charge_full_design" from EC will not be added, and
  // "manufacturer" and "model_name" will not be updated.
  auto ans = CreateProbeResultFromJson(R"JSON(
    [
      {
        "manufacturer": "123-ABC",
        "model_name": "XYZ-00000",
        "technology": "Li-poly",
        "type": "Battery"
      }
    ]
  )JSON");
  ans[0].GetDict().Set("path", GetPathUnderRoot(bat0_path).value());

  base::Value probe_statement(base::Value::Type::DICTIONARY);
  auto probe_function = CreateProbeFunction<GenericBattery>(probe_statement);
  auto result = probe_function->Eval();
  EXPECT_EQ(result, ans);
}

TEST_F(GenericBatteryTest, ParseEctoolBatteryFailed) {
  constexpr auto invalid_ectool_battery = "Battery info:\n";
  auto debugd = mock_context()->mock_debugd_proxy();
  EXPECT_CALL(*debugd, BatteryFirmware("info", _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(invalid_ectool_battery), Return(true)));

  auto ans = CreateProbeResultFromJson(R"JSON(
    [
      {
        "manufacturer": "123-ABC",
        "model_name": "XYZ-00000",
        "technology": "Li-poly",
        "type": "Battery"
      }
    ]
  )JSON");
  ans[0].GetDict().Set("path", GetPathUnderRoot(bat0_path).value());

  base::Value probe_statement(base::Value::Type::DICTIONARY);
  auto probe_function = CreateProbeFunction<GenericBattery>(probe_statement);
  auto result = probe_function->Eval();
  EXPECT_EQ(result, ans);
}

}  // namespace
}  // namespace runtime_probe
