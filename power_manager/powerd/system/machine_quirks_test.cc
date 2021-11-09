// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/machine_quirks.h"

#include <memory>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

#include "power_manager/common/fake_prefs.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/common/util.h"

namespace power_manager {
namespace system {

namespace {

const char sp_models[] =
    "OptiPlex 740\n"
    "ThinkPad X120e\n"
    "HP Compaq dc7900";
const char sti_models[] =
    "Compaq dc7800\n"
    "compaq dc5800\n"
    "OptiPlex 7020\n"
    "OptiPlex 9010\n"
    "OptiPlex 9020\n"
    "HP Compaq 6000 Pro\n"
    "HP Compaq 8000 Elite\n"
    "ThinkCentre M93\n"
    "ProDesk 600 G1\n"
    "Surface Pro 3\n";
}  // namespace

class MachineQuirksTest : public ::testing::Test {
 public:
  MachineQuirksTest() {
    // Create mock directories
    CHECK(temp_dir_.CreateUniqueTempDir());
    dmi_id_dir_ = temp_dir_.GetPath().Append("sys/class/dmi/id/");
    CHECK(base::CreateDirectory(dmi_id_dir_));
    pm_dir_ = temp_dir_.GetPath().Append("usr/share/power_manager/");
    CHECK(base::CreateDirectory(pm_dir_));

    // Move device lists into mock power_manager directory
    base::WriteFile(pm_dir_.Append("suspend_prevention_models"), sp_models,
                    strlen(sp_models));
    base::WriteFile(pm_dir_.Append("suspend_to_idle_models"), sti_models,
                    strlen(sti_models));

    // Set up mock prefs default values
    prefs_.SetInt64(kDisableIdleSuspendPref, 0);
    prefs_.SetInt64(kSuspendToIdlePref, 0);
    prefs_.SetInt64(kHasMachineQuirks, 1);
  }

  MachineQuirksTest(const MachineQuirksTest&) = delete;
  MachineQuirksTest& operator=(const MachineQuirksTest&) = delete;

  ~MachineQuirksTest() override = default;

 protected:
  std::unique_ptr<MachineQuirks> CreateMachineQuirks() {
    auto machine_quirks = std::make_unique<MachineQuirks>();
    machine_quirks->set_dmi_id_dir_for_test(dmi_id_dir_);
    machine_quirks->set_pm_dir_for_test(pm_dir_);
    return machine_quirks;
  }

  void CreateDmiFile(std::string name, std::string data) {
    base::FilePath file_name = base::FilePath(name);
    base::CreateTemporaryFileInDir(dmi_id_dir_, &file_name);
    ASSERT_TRUE(util::WriteFileFully(dmi_id_dir_.Append(name), data.c_str(),
                                     data.size()));
  }

  base::ScopedTempDir temp_dir_;
  base::FilePath dmi_id_dir_;
  base::FilePath pm_dir_;
  FakePrefs prefs_;
};

// Tests IsQuirkMatch function by inputing strings that are or aren't on the
// device lists
TEST_F(MachineQuirksTest, IsQuirkMatch) {
  auto quirks = CreateMachineQuirks();

  std::string file_contents;
  base::ReadFileToString(
      base::FilePath(pm_dir_.Append("suspend_prevention_models")),
      &file_contents);
  EXPECT_EQ(true, quirks->IsQuirkMatch("OptiPlex 740", file_contents));
  EXPECT_EQ(true, quirks->IsQuirkMatch("HP Compaq dc7900", file_contents));
  EXPECT_EQ(false, quirks->IsQuirkMatch("OptiPlex", file_contents));

  base::ReadFileToString(
      base::FilePath(pm_dir_.Append("suspend_to_idle_models")), &file_contents);
  EXPECT_EQ(true, quirks->IsQuirkMatch("Compaq dc7800", file_contents));
  EXPECT_EQ(true, quirks->IsQuirkMatch("Surface Pro 3", file_contents));
  EXPECT_EQ(false, quirks->IsQuirkMatch("HP Compaq dc7900", file_contents));
}

// Test that IsSuspendToIdle is true when the dmi value is a match
TEST_F(MachineQuirksTest, IsSuspendToIdleTrue) {
  auto quirks = CreateMachineQuirks();
  CreateDmiFile("product_name", "OptiPlex 7020");
  EXPECT_EQ(true, quirks->IsSuspendToIdle());
  // Also test for the case when there is whitespace added
  CreateDmiFile("product_name", " OptiPlex 7020 ");
  EXPECT_EQ(true, quirks->IsSuspendToIdle());
}

// Test that IsSuspendToIdle is false when there is no matching dmi value
TEST_F(MachineQuirksTest, IsSuspendToIdleFalse) {
  auto quirks = CreateMachineQuirks();
  EXPECT_EQ(false, quirks->IsSuspendToIdle());
  CreateDmiFile("product_name", "foo");
  EXPECT_EQ(false, quirks->IsSuspendToIdle());
}

// Test that IsSuspendBlocked is true when the dmi value is a match
TEST_F(MachineQuirksTest, IsSuspendBlockedTrue) {
  auto quirks = CreateMachineQuirks();
  CreateDmiFile("product_name", "HP Compaq dc7900");
  EXPECT_EQ(true, quirks->IsSuspendBlocked());
  // Also test for the case when there is whitespace added
  CreateDmiFile("product_name", " HP Compaq dc7900 ");
  EXPECT_EQ(true, quirks->IsSuspendBlocked());
}

// Test that IsSuspendBlocked is false when there is no matching dmi value
TEST_F(MachineQuirksTest, IsSuspendBlockedFalse) {
  auto quirks = CreateMachineQuirks();
  EXPECT_EQ(false, quirks->IsSuspendBlocked());
  CreateDmiFile("product_name", "foo");
  EXPECT_EQ(false, quirks->IsSuspendBlocked());
}

// Testing that when kHasMachineQuirks = 0, then no quirks are applied
TEST_F(MachineQuirksTest, MachineQuirksDisabled) {
  auto quirks = CreateMachineQuirks();
  CreateDmiFile("product_name", "HP Compaq dc7900");
  prefs_.SetInt64(kHasMachineQuirks, 0);
  quirks->ApplyQuirksToPrefs(&prefs_);
  int64_t disable_idle_suspend_pref = 2;
  int64_t suspend_to_idle_pref = 2;
  CHECK(prefs_.GetInt64(kDisableIdleSuspendPref, &disable_idle_suspend_pref));
  CHECK(prefs_.GetInt64(kSuspendToIdlePref, &suspend_to_idle_pref));
  EXPECT_EQ(0, disable_idle_suspend_pref);
  EXPECT_EQ(0, suspend_to_idle_pref);
}

// Testing that the correct pref is set when there aren't any quirk matches
TEST_F(MachineQuirksTest, ApplyQuirksToPrefsNone) {
  auto quirks = CreateMachineQuirks();
  quirks->ApplyQuirksToPrefs(&prefs_);
  int64_t disable_idle_suspend_pref = 2;
  int64_t suspend_to_idle_pref = 2;
  CHECK(prefs_.GetInt64(kDisableIdleSuspendPref, &disable_idle_suspend_pref));
  CHECK(prefs_.GetInt64(kSuspendToIdlePref, &suspend_to_idle_pref));
  EXPECT_EQ(0, disable_idle_suspend_pref);
  EXPECT_EQ(0, suspend_to_idle_pref);
}

// Testing that the correct pref is set when there's a suspend blocked match
TEST_F(MachineQuirksTest, ApplyQuirksToPrefsWhenSuspendIsBlocked) {
  auto quirks = CreateMachineQuirks();
  CreateDmiFile("product_name", "HP Compaq dc7900");
  quirks->ApplyQuirksToPrefs(&prefs_);
  int64_t disable_idle_suspend_pref = 2;
  int64_t suspend_to_idle_pref = 2;

  CHECK(prefs_.GetInt64(kDisableIdleSuspendPref, &disable_idle_suspend_pref));
  CHECK(prefs_.GetInt64(kSuspendToIdlePref, &suspend_to_idle_pref));
  EXPECT_EQ(1, disable_idle_suspend_pref);
  EXPECT_EQ(0, suspend_to_idle_pref);
}

// Testing that the correct pref is set when there's a suspend to idle match
TEST_F(MachineQuirksTest, ApplyQuirksToPrefsWhenIsSuspendToIdle) {
  auto quirks = CreateMachineQuirks();
  CreateDmiFile("product_name", "OptiPlex 7020");
  quirks->ApplyQuirksToPrefs(&prefs_);
  int64_t disable_idle_suspend_pref = 2;
  int64_t suspend_to_idle_pref = 2;

  CHECK(prefs_.GetInt64(kDisableIdleSuspendPref, &disable_idle_suspend_pref));
  CHECK(prefs_.GetInt64(kSuspendToIdlePref, &suspend_to_idle_pref));
  EXPECT_EQ(0, disable_idle_suspend_pref);
  EXPECT_EQ(1, suspend_to_idle_pref);
}

}  // namespace system
}  // namespace power_manager
