// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/machine_quirks.h"

#include <cstdint>
#include <string_view>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

#include "power_manager/common/fake_prefs.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/common/util.h"
#include "power_manager/powerd/testing/test_environment.h"

namespace power_manager::system {

namespace {

const char sp_models[] =
    "board_name:valA\n"
    "ThinkPad X120e\n"
    "HP Compaq dc7900";
const char sti_models[] =
    "compaq dc5800\n"
    "OptiPlex 7020\n"
    "OptiPlex 9010\n"
    "OptiPlex 9020\n"
    "HP Compaq 6000 Pro\n"
    "HP Compaq 8000 Elite\n"
    "ThinkCentre M93\n"
    "ProDesk 600 G1\n";
const char external_display_models[] =
    "HP Engage Go 13.5 inch Mobile System\n"
    "board_name:valA, product_family:valB\n";
constexpr std::string_view valid_battery_type = "Battery";
constexpr std::string_view invalid_battery_type = "Wireless";
constexpr std::string_view valid_battery_uevent_file =
    "DRIVER=battery\n"
    "MODALIAS=acpi:PNP0C0A:\n";
constexpr std::string_view invalid_battery_uevent_file =
    "DRIVER=foo\n"
    "MODALIAS=acpi:PNP0C0A:\n";
constexpr std::string_view peripheral_battery_scope = "Device";
}  // namespace

class MachineQuirksTest : public TestEnvironment {
 public:
  MachineQuirksTest() {
    // Create mock directories
    CHECK(temp_dir_.CreateUniqueTempDir());
    dmi_id_dir_ = temp_dir_.GetPath().Append("sys/class/dmi/id/");
    CHECK(base::CreateDirectory(dmi_id_dir_));
    power_supply_dir_ = temp_dir_.GetPath().Append("sys/class/power_supply/");
    CHECK(base::CreateDirectory(power_supply_dir_));

    // Tell machine_quirks_ what directories to use
    machine_quirks_.set_dmi_id_dir_for_test(dmi_id_dir_);
    machine_quirks_.set_power_supply_dir_for_test(power_supply_dir_);

    // Set up mock pref device lists
    prefs_.SetString(kSuspendPreventionListPref, sp_models);
    prefs_.SetString(kSuspendToIdleListPref, sti_models);
    prefs_.SetString(kExternalDisplayOnlyListPref, external_display_models);

    // Set up mock prefs default values
    prefs_.SetInt64(kHasMachineQuirksPref, 1);
    prefs_.SetInt64(kDisableIdleSuspendPref, 0);
    prefs_.SetInt64(kSuspendToIdlePref, 0);
    prefs_.SetInt64(kExternalDisplayOnlyPref, 0);
    prefs_.SetInt64(kAllowZeroChargeReadOnACPref, 0);

    // Init machine_quirks_ with prefs
    machine_quirks_.Init(&prefs_);
  }

  MachineQuirksTest(const MachineQuirksTest&) = delete;
  MachineQuirksTest& operator=(const MachineQuirksTest&) = delete;

  ~MachineQuirksTest() override = default;

 protected:
  void CreateDmiFile(std::string name, std::string data) {
    base::FilePath file_name = base::FilePath(name);
    base::CreateTemporaryFileInDir(dmi_id_dir_, &file_name);
    ASSERT_TRUE(base::WriteFile(dmi_id_dir_.Append(name), data));
  }

  void CreateDirAndWriteFile(const base::FilePath& path,
                             const std::string_view contents) {
    CHECK(base::CreateDirectory(path.DirName()));
    CHECK(base::WriteFile(path, contents));
  }

  base::ScopedTempDir temp_dir_;
  base::FilePath dmi_id_dir_;
  base::FilePath power_supply_dir_;
  FakePrefs prefs_;
  MachineQuirks machine_quirks_;
};

// Test that IsSuspendToIdle is true when the dmi value is a match
TEST_F(MachineQuirksTest, IsSuspendToIdleTrue) {
  CreateDmiFile("product_name", "OptiPlex 7020");
  EXPECT_EQ(true, machine_quirks_.IsSuspendToIdle());
  // Also test for the case when there is whitespace added
  CreateDmiFile("product_name", " OptiPlex 7020 ");
  EXPECT_EQ(true, machine_quirks_.IsSuspendToIdle());
}

// Test that IsSuspendToIdle is false when there is no matching dmi value
TEST_F(MachineQuirksTest, IsSuspendToIdleFalse) {
  EXPECT_EQ(false, machine_quirks_.IsSuspendToIdle());
  CreateDmiFile("product_name", "foo");
  EXPECT_EQ(false, machine_quirks_.IsSuspendToIdle());
}

// Test that IsSuspendBlocked is true when the dmi value is a match
TEST_F(MachineQuirksTest, IsSuspendBlockedTrue) {
  CreateDmiFile("board_name", "valA");
  EXPECT_EQ(true, machine_quirks_.IsSuspendBlocked());
}

// Test that IsSuspendBlocked is false when there is no matching dmi value
TEST_F(MachineQuirksTest, IsSuspendBlockedFalse) {
  EXPECT_EQ(false, machine_quirks_.IsSuspendBlocked());
  CreateDmiFile("product_name", "foo");
  EXPECT_EQ(false, machine_quirks_.IsSuspendBlocked());
}

// Test that IsExternalDisplayOnly is true when the dmi value is a match
TEST_F(MachineQuirksTest, IsExternalDisplayOnlyTrue) {
  // Test for the case when there are multiple dmi values
  CreateDmiFile("board_name", "valA");
  CreateDmiFile("product_family", "valB");
  EXPECT_EQ(true, machine_quirks_.IsExternalDisplayOnly());
}

// Test that IsExternalDisplayOnly is false when there is a mismatch
TEST_F(MachineQuirksTest, IsExternalDisplayOnlyFalse) {
  // Test for the case of a partial mismatch
  EXPECT_EQ(false, machine_quirks_.IsExternalDisplayOnly());
  CreateDmiFile("board_name", "valA");
  CreateDmiFile("product_family", "foo");
  EXPECT_EQ(false, machine_quirks_.IsExternalDisplayOnly());
}

// Test that IsGenericAcpiBatteryDriver is true when the generic battery driver
// is present.
TEST_F(MachineQuirksTest, IsGenericAcpiBatteryDriverTrue) {
  CreateDirAndWriteFile(power_supply_dir_.Append("BAT0/type"),
                        valid_battery_type);
  CreateDirAndWriteFile(
      power_supply_dir_.Append("BAT0/device/driver/PNP0C0A:00/uevent"),
      valid_battery_uevent_file);
  EXPECT_TRUE(machine_quirks_.IsGenericAcpiBatteryDriver());
}

// Test that IsGenericAcpiBatteryDriver is false when the only devices found
// do not have the "Battery" type.
TEST_F(MachineQuirksTest, FalseIfDeviceTypeBad) {
  CreateDirAndWriteFile(power_supply_dir_.Append("BAT0/type"),
                        invalid_battery_type);
  CreateDirAndWriteFile(
      power_supply_dir_.Append("BAT0/device/driver/PNP0C0A:00/uevent"),
      valid_battery_uevent_file);
  EXPECT_FALSE(machine_quirks_.IsGenericAcpiBatteryDriver());
}

// Test that IsGenericAcpiBatteryDriver is false when the only devices found
// have incorrect scope.
TEST_F(MachineQuirksTest, FalseIfDeviceScopeBad) {
  CreateDirAndWriteFile(power_supply_dir_.Append("BAT0/type"),
                        valid_battery_type);
  CreateDirAndWriteFile(power_supply_dir_.Append("BAT0/scope"),
                        peripheral_battery_scope);
  CreateDirAndWriteFile(
      power_supply_dir_.Append("BAT0/device/driver/PNP0C0A:00/uevent"),
      valid_battery_uevent_file);
  EXPECT_FALSE(machine_quirks_.IsGenericAcpiBatteryDriver());
}

// Test that IsGenericAcpiBatteryDriver is false when there are multiple
// batteries.
TEST_F(MachineQuirksTest, DontApplyIfMultipleBatteries) {
  CreateDirAndWriteFile(power_supply_dir_.Append("BAT0/type"),
                        valid_battery_type);
  CreateDirAndWriteFile(power_supply_dir_.Append("BAT1/type"),
                        valid_battery_type);
  CreateDirAndWriteFile(
      power_supply_dir_.Append("BAT0/device/driver/PNP0C0A:00/uevent"),
      valid_battery_uevent_file);
  CreateDirAndWriteFile(
      power_supply_dir_.Append("BAT1/device/driver/PNP0C0A:01/uevent"),
      valid_battery_uevent_file);
  EXPECT_FALSE(machine_quirks_.IsGenericAcpiBatteryDriver());
}

// Test that IsGenericAcpiBatteryDriver is false when the driver doesn't match
// the regex.
TEST_F(MachineQuirksTest, IsNotGenericBatteryDriver) {
  CreateDirAndWriteFile(power_supply_dir_.Append("BAT0/type"),
                        valid_battery_type);
  CreateDirAndWriteFile(
      power_supply_dir_.Append("BAT0/device/driver/PNP0C0A:00/uevent"),
      invalid_battery_uevent_file);
  EXPECT_FALSE(machine_quirks_.IsGenericAcpiBatteryDriver());
}

// Testing that when kHasMachineQuirksPref = 0, then no quirks are applied
TEST_F(MachineQuirksTest, MachineQuirksDisabled) {
  CreateDmiFile("product_name", "HP Compaq dc7900");
  prefs_.SetInt64(kHasMachineQuirksPref, 0);
  machine_quirks_.ApplyQuirksToPrefs();

  int64_t disable_idle_suspend_pref = 2;
  int64_t suspend_to_idle_pref = 2;
  int64_t external_display_only_pref = 2;
  int64_t zero_charge_pref = 2;
  CHECK(prefs_.GetInt64(kDisableIdleSuspendPref, &disable_idle_suspend_pref));
  CHECK(prefs_.GetInt64(kSuspendToIdlePref, &suspend_to_idle_pref));
  CHECK(prefs_.GetInt64(kExternalDisplayOnlyPref, &external_display_only_pref));
  CHECK(prefs_.GetInt64(kAllowZeroChargeReadOnACPref, &zero_charge_pref));

  EXPECT_EQ(0, disable_idle_suspend_pref);
  EXPECT_EQ(0, suspend_to_idle_pref);
  EXPECT_EQ(0, external_display_only_pref);
  EXPECT_EQ(0, zero_charge_pref);
}

// Testing that the correct pref is set when there aren't any quirk matches
TEST_F(MachineQuirksTest, ApplyQuirksToPrefsNone) {
  machine_quirks_.ApplyQuirksToPrefs();

  int64_t disable_idle_suspend_pref = 2;
  int64_t suspend_to_idle_pref = 2;
  int64_t external_display_only_pref = 2;
  int64_t zero_charge_pref = 2;
  CHECK(prefs_.GetInt64(kDisableIdleSuspendPref, &disable_idle_suspend_pref));
  CHECK(prefs_.GetInt64(kSuspendToIdlePref, &suspend_to_idle_pref));
  CHECK(prefs_.GetInt64(kExternalDisplayOnlyPref, &external_display_only_pref));
  CHECK(prefs_.GetInt64(kAllowZeroChargeReadOnACPref, &zero_charge_pref));

  EXPECT_EQ(0, disable_idle_suspend_pref);
  EXPECT_EQ(0, suspend_to_idle_pref);
  EXPECT_EQ(0, external_display_only_pref);
  EXPECT_EQ(0, zero_charge_pref);
}

// Testing that the correct pref is set when there's a suspend blocked match
TEST_F(MachineQuirksTest, ApplyQuirksToPrefsWhenSuspendIsBlocked) {
  CreateDmiFile("product_name", "HP Compaq dc7900");
  machine_quirks_.ApplyQuirksToPrefs();

  int64_t disable_idle_suspend_pref = 2;
  int64_t suspend_to_idle_pref = 2;
  CHECK(prefs_.GetInt64(kDisableIdleSuspendPref, &disable_idle_suspend_pref));
  CHECK(prefs_.GetInt64(kSuspendToIdlePref, &suspend_to_idle_pref));

  EXPECT_EQ(1, disable_idle_suspend_pref);
  EXPECT_EQ(0, suspend_to_idle_pref);
}

// Testing that the correct pref is set when there's a suspend to idle match
TEST_F(MachineQuirksTest, ApplyQuirksToPrefsWhenIsSuspendToIdle) {
  CreateDmiFile("product_name", "OptiPlex 7020");
  machine_quirks_.ApplyQuirksToPrefs();

  int64_t disable_idle_suspend_pref = 2;
  int64_t suspend_to_idle_pref = 2;
  CHECK(prefs_.GetInt64(kDisableIdleSuspendPref, &disable_idle_suspend_pref));
  CHECK(prefs_.GetInt64(kSuspendToIdlePref, &suspend_to_idle_pref));

  EXPECT_EQ(0, disable_idle_suspend_pref);
  EXPECT_EQ(1, suspend_to_idle_pref);
}

// Testing that the correct pref is set when there's a external display only
// match
TEST_F(MachineQuirksTest, ApplyQuirksToPrefsWhenIsExternalDisplayOnly) {
  CreateDmiFile("product_name", "HP Engage Go 13.5 inch Mobile System");
  machine_quirks_.ApplyQuirksToPrefs();

  int64_t disable_idle_suspend_pref = 2;
  int64_t suspend_to_idle_pref = 2;
  int64_t external_display_only_pref = 2;
  CHECK(prefs_.GetInt64(kDisableIdleSuspendPref, &disable_idle_suspend_pref));
  CHECK(prefs_.GetInt64(kSuspendToIdlePref, &suspend_to_idle_pref));
  CHECK(prefs_.GetInt64(kExternalDisplayOnlyPref, &external_display_only_pref));

  EXPECT_EQ(0, disable_idle_suspend_pref);
  EXPECT_EQ(0, suspend_to_idle_pref);
  EXPECT_EQ(1, external_display_only_pref);
}

// Testing that the correct pref is set when the generic battery driver
// is present.
TEST_F(MachineQuirksTest, ApplyQuirksToPrefsWhenIsGenericBatteryDriverOnly) {
  CreateDirAndWriteFile(power_supply_dir_.Append("BAT0/type"),
                        valid_battery_type);
  CreateDirAndWriteFile(
      power_supply_dir_.Append("BAT0/device/driver/PNP0C0A:00/uevent"),
      valid_battery_uevent_file);
  machine_quirks_.ApplyQuirksToPrefs();

  int64_t disable_idle_suspend_pref = 2;
  int64_t suspend_to_idle_pref = 2;
  int64_t external_display_only_pref = 2;
  int64_t zero_charge_pref = 2;
  CHECK(prefs_.GetInt64(kDisableIdleSuspendPref, &disable_idle_suspend_pref));
  CHECK(prefs_.GetInt64(kSuspendToIdlePref, &suspend_to_idle_pref));
  CHECK(prefs_.GetInt64(kExternalDisplayOnlyPref, &external_display_only_pref));
  CHECK(prefs_.GetInt64(kAllowZeroChargeReadOnACPref, &zero_charge_pref));

  EXPECT_EQ(0, disable_idle_suspend_pref);
  EXPECT_EQ(0, suspend_to_idle_pref);
  EXPECT_EQ(0, external_display_only_pref);
  EXPECT_EQ(1, zero_charge_pref);
}

}  // namespace power_manager::system
