//
// Copyright (C) 2016 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "update_engine/cros/hardware_chromeos.h"

#include <memory>
#include <optional>
#include <utility>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/json/json_string_value_serializer.h>
#include <brillo/file_utils.h>
#include <gtest/gtest.h>
#include <libcrossystem/crossystem.h>
#include <libcrossystem/crossystem_fake.h>

#include "update_engine/common/constants.h"
#include "update_engine/common/fake_hardware.h"
#include "update_engine/common/platform_constants.h"
#include "update_engine/common/test_utils.h"
#include "update_engine/cros/fake_system_state.h"
#include "update_engine/update_manager/umtest_utils.h"

using chromeos_update_engine::test_utils::WriteFileString;
using std::string;

namespace {

constexpr char kEnrollmentReCoveryTrueJSON[] = R"({
  "the_list": [ "val1", "val2" ],
  "EnrollmentRecoveryRequired": true,
  "some_String": "1337",
  "some_int": 42
})";

constexpr char kEnrollmentReCoveryFalseJSON[] = R"({
  "the_list": [ "val1", "val2" ],
  "EnrollmentRecoveryRequired": false,
  "some_String": "1337",
  "some_int": 42
})";

constexpr char kNoEnrollmentRecoveryJSON[] = R"({
  "the_list": [ "val1", "val2" ],
  "some_String": "1337",
  "some_int": 42
})";

constexpr char kConsumerSegmentTrueJSON[] = R"({
  "IsConsumerSegment": true
})";

constexpr char kConsumerSegmentFalseJSON[] = R"({
  "IsConsumerSegment": false
})";

constexpr char kNoConsumerSegmentJSON[] = "";

}  // namespace

namespace chromeos_update_engine {

class HardwareChromeOSTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(root_dir_.CreateUniqueTempDir());
    FakeSystemState::CreateInstance();

    auto fake_crossystem_impl =
        std::make_unique<crossystem::fake::CrossystemFake>();
    auto crossystem = std::make_unique<crossystem::Crossystem>(
        std::move(fake_crossystem_impl));
    hardware_.crossystem_ = std::move(crossystem);
  }

  void WriteStatefulConfig(const string& config) {
    base::FilePath kFile(root_dir_.GetPath().value() + kStatefulPartition +
                         "/etc/update_manager.conf");
    ASSERT_TRUE(base::CreateDirectory(kFile.DirName()));
    ASSERT_TRUE(WriteFileString(kFile.value(), config));
  }

  void WriteRootfsConfig(const string& config) {
    base::FilePath kFile(root_dir_.GetPath().value() +
                         "/etc/update_manager.conf");
    ASSERT_TRUE(base::CreateDirectory(kFile.DirName()));
    ASSERT_TRUE(WriteFileString(kFile.value(), config));
  }

  // Helper method to call HardwareChromeOS::LoadConfig with the test directory.
  void CallLoadConfig(bool normal_mode) {
    hardware_.LoadConfig(root_dir_.GetPath().value(), normal_mode);
  }

  std::unique_ptr<base::Value> JSONToUniquePtrValue(const string& json_string) {
    int error_code;
    std::string error_msg;

    JSONStringValueDeserializer deserializer(json_string);

    return deserializer.Deserialize(&error_code, &error_msg);
  }

  HardwareChromeOS hardware_;
  base::ScopedTempDir root_dir_;
};

TEST_F(HardwareChromeOSTest, NoLocalFile) {
  std::unique_ptr<base::Value> root = nullptr;

  EXPECT_FALSE(hardware_.IsEnrollmentRecoveryModeEnabled(root.get()));
}

TEST_F(HardwareChromeOSTest, LocalFileWithEnrollmentRecoveryTrue) {
  std::unique_ptr<base::Value> root =
      JSONToUniquePtrValue(kEnrollmentReCoveryTrueJSON);
  EXPECT_TRUE(hardware_.IsEnrollmentRecoveryModeEnabled(root.get()));
}

TEST_F(HardwareChromeOSTest, LocalFileWithEnrollmentRecoveryFalse) {
  std::unique_ptr<base::Value> root =
      JSONToUniquePtrValue(kEnrollmentReCoveryFalseJSON);
  EXPECT_FALSE(hardware_.IsEnrollmentRecoveryModeEnabled(root.get()));
}

TEST_F(HardwareChromeOSTest, LocalFileWithNoEnrollmentRecoveryPath) {
  std::unique_ptr<base::Value> root =
      JSONToUniquePtrValue(kNoEnrollmentRecoveryJSON);
  EXPECT_FALSE(hardware_.IsEnrollmentRecoveryModeEnabled(root.get()));
}

TEST_F(HardwareChromeOSTest, NoFileFoundReturnsDefault) {
  CallLoadConfig(true /* normal_mode */);
  EXPECT_TRUE(hardware_.IsOOBEEnabled());
}

TEST_F(HardwareChromeOSTest, DontReadStatefulInNormalMode) {
  WriteStatefulConfig("is_oobe_enabled=false");

  CallLoadConfig(true /* normal_mode */);
  EXPECT_TRUE(hardware_.IsOOBEEnabled());
}

TEST_F(HardwareChromeOSTest, ReadStatefulInDevMode) {
  WriteRootfsConfig("is_oobe_enabled=true");
  // Since the stateful is present, we should read that one.
  WriteStatefulConfig("is_oobe_enabled=false");

  CallLoadConfig(false /* normal_mode */);
  EXPECT_FALSE(hardware_.IsOOBEEnabled());
}

TEST_F(HardwareChromeOSTest, ReadRootfsIfStatefulNotFound) {
  WriteRootfsConfig("is_oobe_enabled=false");

  CallLoadConfig(false /* normal_mode */);
  EXPECT_FALSE(hardware_.IsOOBEEnabled());
}

TEST_F(HardwareChromeOSTest, RunningInMiniOs) {
  base::FilePath test_path = root_dir_.GetPath();
  hardware_.SetRootForTest(test_path);
  std::string cmdline =
      " loglevel=7    root=/dev cros_minios \"noinitrd "
      "panic=60   version=14018.0\" \'kern_guid=78 ";
  brillo::TouchFile(test_path.Append("proc").Append("cmdline"));
  EXPECT_TRUE(
      base::WriteFile(test_path.Append("proc").Append("cmdline"), cmdline));
  EXPECT_TRUE(hardware_.IsRunningFromMiniOs());

  cmdline = " loglevel=7    root=/dev cros_minios";
  EXPECT_TRUE(
      base::WriteFile(test_path.Append("proc").Append("cmdline"), cmdline));
  EXPECT_TRUE(hardware_.IsRunningFromMiniOs());

  // Search all matches for key.
  cmdline = "cros_minios_version=1.1.1 cros_minios";
  EXPECT_TRUE(
      base::WriteFile(test_path.Append("proc").Append("cmdline"), cmdline));
  EXPECT_TRUE(hardware_.IsRunningFromMiniOs());

  // Ends with quotes.
  cmdline =
      "dm_verity.dev_wait=1  \"noinitrd panic=60 "
      "cros_minios_version=14116.0.2021_07_28_1259 cros_minios\"";
  EXPECT_TRUE(
      base::WriteFile(test_path.Append("proc").Append("cmdline"), cmdline));
  EXPECT_TRUE(hardware_.IsRunningFromMiniOs());

  // Search all matches for key, reject multiple partial matches.
  cmdline = "cros_minios_version=1.1.1 cros_minios_mode";
  EXPECT_TRUE(
      base::WriteFile(test_path.Append("proc").Append("cmdline"), cmdline));
  EXPECT_FALSE(hardware_.IsRunningFromMiniOs());

  // Reject a partial match.
  cmdline = " loglevel=7    root=/dev cros_minios_version=1.1.1";
  EXPECT_TRUE(
      base::WriteFile(test_path.Append("proc").Append("cmdline"), cmdline));
  EXPECT_FALSE(hardware_.IsRunningFromMiniOs());
}

TEST_F(HardwareChromeOSTest, NotRunningInMiniOs) {
  EXPECT_FALSE(hardware_.IsRunningFromMiniOs());
}

TEST_F(HardwareChromeOSTest, RecoveryKeyVersionMissingFile) {
  base::FilePath test_path = root_dir_.GetPath();
  hardware_.SetNonVolatileDirectoryForTest(test_path);

  base::FilePath non_volatile_directory;
  ASSERT_TRUE(hardware_.GetNonVolatileDirectory(&non_volatile_directory));
  ASSERT_TRUE(base::CreateDirectory(non_volatile_directory));

  std::string version;
  EXPECT_FALSE(hardware_.GetRecoveryKeyVersion(&version));
}

TEST_F(HardwareChromeOSTest, RecoveryKeyVersionBadKey) {
  base::FilePath test_path = root_dir_.GetPath();
  hardware_.SetNonVolatileDirectoryForTest(test_path);

  base::FilePath non_volatile_directory;
  ASSERT_TRUE(hardware_.GetNonVolatileDirectory(&non_volatile_directory));
  ASSERT_TRUE(base::CreateDirectory(non_volatile_directory));

  EXPECT_TRUE(base::WriteFile(
      non_volatile_directory.Append(constants::kRecoveryKeyVersionFileName),
      "foobar"));

  std::string version;
  EXPECT_FALSE(hardware_.GetRecoveryKeyVersion(&version));
}

TEST_F(HardwareChromeOSTest, RecoveryKeyVersion) {
  base::FilePath test_path = root_dir_.GetPath();
  hardware_.SetNonVolatileDirectoryForTest(test_path);

  base::FilePath non_volatile_directory;
  ASSERT_TRUE(hardware_.GetNonVolatileDirectory(&non_volatile_directory));
  ASSERT_TRUE(base::CreateDirectory(non_volatile_directory));

  EXPECT_TRUE(base::WriteFile(
      non_volatile_directory.Append(constants::kRecoveryKeyVersionFileName),
      "123"));

  std::string version;
  EXPECT_TRUE(hardware_.GetRecoveryKeyVersion(&version));
  EXPECT_EQ(std::string("123"), version);
}

TEST_F(HardwareChromeOSTest, RecoveryKeyVersionTrimWhitespaces) {
  base::FilePath test_path = root_dir_.GetPath();
  hardware_.SetNonVolatileDirectoryForTest(test_path);

  base::FilePath non_volatile_directory;
  ASSERT_TRUE(hardware_.GetNonVolatileDirectory(&non_volatile_directory));
  ASSERT_TRUE(base::CreateDirectory(non_volatile_directory));

  EXPECT_TRUE(base::WriteFile(
      non_volatile_directory.Append(constants::kRecoveryKeyVersionFileName),
      "\n888\n"));

  std::string version;
  EXPECT_TRUE(hardware_.GetRecoveryKeyVersion(&version));
  EXPECT_EQ(std::string("888"), version);
}

TEST_F(HardwareChromeOSTest, IsRootfsVerificationEnabled) {
  base::FilePath test_path = root_dir_.GetPath();
  hardware_.SetRootForTest(test_path);
  {
    std::string cmdline =
        R"(cros_secure console= loglevel=7 init=/sbin/init )"
        R"(cros_secure drm.trace=0x106 )"
        R"(root=PARTUUID=dc3f3c92-18db-744b-a2c2-7b0eb696b879/PARTNROFF=1 )"
        R"(rootwait rw dm_verity.error_behavior=3 dm_verity.max_bios=-1 )"
        R"(dm_verity.dev_wait=0 )"
        R"(dm="1 vroot none ro 1,0 6348800 verity payload=ROOT_DEV )"
        R"(hashtree=HASH_DEV hashstart=6348800 alg=sha256 )"
        R"(root_hexdigest=ebc3199685b6f9217c59016b3d4a82ce066c2087a3b99c9c38e9)"
        R"(772281288fb1 )"
        R"(salt=00d2f004a524773dd1c69aada1cc91b9a5de7701ffbfd4fec89ff34469a47)"
        R"(cf0" noinitrd cros_debug vt.global_cursor_default=0 )"
        R"(kern_guid=dc3f3c92-18db-744b-a2c2-7b0eb696b879 add_efi_memmap )"
        R"(noresume i915.modeset=1 ramoops.ecc=1 )"
        R"(tpm_tis.force=0 intel_pmc_core.warn_on_s0ix_failures=1 )"
        R"(i915.enable_guc=3 i915.enable_dc=4 xdomain=0 swiotlb=65536 )"
        R"(intel_iommu=on i915.enable_psr=1 usb-storage.quirks=13fe:6500:u)";
    brillo::TouchFile(test_path.Append("proc").Append("cmdline"));
    EXPECT_TRUE(
        base::WriteFile(test_path.Append("proc").Append("cmdline"), cmdline));
    EXPECT_FALSE(hardware_.IsRootfsVerificationEnabled());
  }

  {
    std::string cmdline =
        R"(cros_secure console= loglevel=7 init=/sbin/init )"
        R"(cros_secure drm.trace=0x106 )"
        R"(root=PARTUUID=dc3f3c92-18db-744b-a2c2-7b0eb696b879/PARTNROFF=1 )"
        R"(rootwait rw dm_verity.error_behavior=3 dm_verity.max_bios=-1 )"
        R"(dm_verity.dev_wait=1 )"
        R"(dm="1 vroot none ro 1,0 6348800 verity payload=ROOT_DEV )"
        R"(hashtree=HASH_DEV hashstart=6348800 alg=sha256 )"
        R"(root_hexdigest=ebc3199685b6f9217c59016b3d4a82ce066c2087a3b99c9c38e9)"
        R"(772281288fb1 )"
        R"(salt=00d2f004a524773dd1c69aada1cc91b9a5de7701ffbfd4fec89ff34469a47)"
        R"(cf0" noinitrd cros_debug vt.global_cursor_default=0 )"
        R"(kern_guid=dc3f3c92-18db-744b-a2c2-7b0eb696b879 add_efi_memmap )"
        R"(noresume i915.modeset=1 ramoops.ecc=1 )"
        R"(tpm_tis.force=0 intel_pmc_core.warn_on_s0ix_failures=1 )"
        R"(i915.enable_guc=3 i915.enable_dc=4 xdomain=0 swiotlb=65536 )"
        R"(intel_iommu=on i915.enable_psr=1 usb-storage.quirks=13fe:6500:u)";
    EXPECT_TRUE(
        base::WriteFile(test_path.Append("proc").Append("cmdline"), cmdline));
    EXPECT_TRUE(hardware_.IsRootfsVerificationEnabled());
  }
}

TEST_F(HardwareChromeOSTest, GeneratePowerwashCommandCheck) {
  constexpr char kExpected[] = "safe fast keepimg reason=update_engine\n";
#if USE_LVM_STATEFUL_PARTITION
  FakeSystemState::Get()->fake_boot_control()->SetIsLvmStackEnabled(false);
  EXPECT_EQ(hardware_.GeneratePowerwashCommand(/*save_rollback_data=*/false),
            kExpected);
  FakeSystemState::Get()->fake_boot_control()->SetIsLvmStackEnabled(true);
  EXPECT_EQ(hardware_.GeneratePowerwashCommand(/*save_rollback_data=*/false),
            "preserve_lvs safe fast keepimg reason=update_engine\n");
#else
  EXPECT_EQ(hardware_.GeneratePowerwashCommand(/*save_rollback_data=*/false),
            kExpected);
#endif  // USE_LVM_STATEFUL_PARTITION
}

TEST_F(HardwareChromeOSTest, GeneratePowerwashCommandWithRollbackDataCheck) {
  constexpr char kExpected[] =
      "safe fast keepimg rollback reason=update_engine\n";
#if USE_LVM_STATEFUL_PARTITION
  FakeSystemState::Get()->fake_boot_control()->SetIsLvmStackEnabled(false);
  EXPECT_EQ(hardware_.GeneratePowerwashCommand(/*save_rollback_data=*/true),
            kExpected);
  FakeSystemState::Get()->fake_boot_control()->SetIsLvmStackEnabled(true);
  EXPECT_EQ(hardware_.GeneratePowerwashCommand(/*save_rollback_data=*/true),
            "preserve_lvs safe fast keepimg rollback reason=update_engine\n");
#else
  EXPECT_EQ(hardware_.GeneratePowerwashCommand(/*save_rollback_data=*/true),
            kExpected);
#endif  // USE_LVM_STATEFUL_PARTITION
}

TEST_F(HardwareChromeOSTest, ConsumerSegmentFalseIfNoLocalStateFile) {
  std::unique_ptr<base::Value> root = nullptr;
  EXPECT_FALSE(hardware_.IsConsumerSegmentSet(root.get()));
}

TEST_F(HardwareChromeOSTest,
       ConsumerSegmentTrueIfLocalFileWithConsumerSegmentTrue) {
  std::unique_ptr<base::Value> root =
      JSONToUniquePtrValue(kConsumerSegmentTrueJSON);
  EXPECT_TRUE(hardware_.IsConsumerSegmentSet(root.get()));
}

TEST_F(HardwareChromeOSTest,
       ConsumerSegmentFalseIfLocalFileWithConsumerSegementFalse) {
  std::unique_ptr<base::Value> root =
      JSONToUniquePtrValue(kConsumerSegmentFalseJSON);
  EXPECT_FALSE(hardware_.IsConsumerSegmentSet(root.get()));
}

TEST_F(HardwareChromeOSTest,
       ConsumerSegmentFalseIfLocalFileWithNoConsumerSegmentPath) {
  std::unique_ptr<base::Value> root =
      JSONToUniquePtrValue(kNoConsumerSegmentJSON);
  EXPECT_FALSE(hardware_.IsConsumerSegmentSet(root.get()));
}

struct FWTestCase {
  std::string mainfw_act;
  std::string fw_try_next;
};

class HardwareChromeOSFWTest : public HardwareChromeOSTest,
                               public testing::WithParamInterface<FWTestCase> {
};

INSTANTIATE_TEST_SUITE_P(
    /* no prefix */,
    HardwareChromeOSFWTest,
    testing::Values(FWTestCase{.mainfw_act = "A", .fw_try_next = "B"},
                    FWTestCase{.mainfw_act = "B", .fw_try_next = "A"}));

TEST_P(HardwareChromeOSFWTest, ResetsFWTryNextSlotProperlyIfValidMainFwAct) {
  auto fw_test_case = GetParam();

  hardware_.crossystem_->VbSetSystemPropertyInt("fw_try_count", 5);
  hardware_.crossystem_->VbSetSystemPropertyString("mainfw_act",
                                                   fw_test_case.mainfw_act);
  hardware_.crossystem_->VbSetSystemPropertyString("fw_try_next",
                                                   fw_test_case.fw_try_next);
  hardware_.crossystem_->VbSetSystemPropertyString("fw_result", "unknown");

  bool result = hardware_.ResetFWTryNextSlot();

  ASSERT_TRUE(result);
  EXPECT_EQ(hardware_.crossystem_->VbGetSystemPropertyString("mainfw_act"),
            fw_test_case.mainfw_act);
  EXPECT_EQ(hardware_.crossystem_->VbGetSystemPropertyString("fw_try_next"),
            hardware_.crossystem_->VbGetSystemPropertyString("mainfw_act"));
  EXPECT_EQ(hardware_.crossystem_->VbGetSystemPropertyInt("fw_try_count"), 0);
  EXPECT_EQ(hardware_.crossystem_->VbGetSystemPropertyString("fw_result"),
            "success");
}

TEST_F(HardwareChromeOSTest, ResetFWTryNextSlotFailsIfInvalidMainFwAct) {
  hardware_.crossystem_->VbSetSystemPropertyString("mainfw_act", "recovery");

  bool result = hardware_.ResetFWTryNextSlot();

  ASSERT_FALSE(result);
}

TEST_F(HardwareChromeOSTest, ResetFWTryNextSlotFailsIfMissingMainFwAct) {
  auto fake_crossystem = std::make_unique<crossystem::fake::CrossystemFake>();
  fake_crossystem->UnsetSystemPropertyValue("mainfw_act");
  hardware_.crossystem_ =
      std::make_unique<crossystem::Crossystem>(std::move(fake_crossystem));

  bool result = hardware_.ResetFWTryNextSlot();

  ASSERT_FALSE(result);
}

TEST_F(HardwareChromeOSTest, ResetFWTryNextSlotFailsIfSettingResultFlagFails) {
  auto fake_crossystem = std::make_unique<crossystem::fake::CrossystemFake>();
  fake_crossystem->SetSystemPropertyReadOnlyStatus("fw_result", true);
  hardware_.crossystem_ =
      std::make_unique<crossystem::Crossystem>(std::move(fake_crossystem));
  hardware_.crossystem_->VbSetSystemPropertyString("mainfw_act", "A");

  bool result = hardware_.ResetFWTryNextSlot();

  ASSERT_FALSE(result);
}

TEST_F(HardwareChromeOSTest, ResetFWTryNextSlotFailsIfSettingTryCountFails) {
  auto fake_crossystem = std::make_unique<crossystem::fake::CrossystemFake>();
  fake_crossystem->SetSystemPropertyReadOnlyStatus("fw_try_count", true);
  hardware_.crossystem_ =
      std::make_unique<crossystem::Crossystem>(std::move(fake_crossystem));
  hardware_.crossystem_->VbSetSystemPropertyString("mainfw_act", "A");

  bool result = hardware_.ResetFWTryNextSlot();

  ASSERT_FALSE(result);
}

TEST_F(HardwareChromeOSTest, IsPowerwashScheduledByUpdateEngineValidReason) {
  base::FilePath root_path = root_dir_.GetPath();
  hardware_.SetRootForTest(root_path);
  base::FilePath marker_path = hardware_.GetPowerwashMarkerFullPath();
  std::string marker_contents = "safe reason=update_engine\n test_key";
  ASSERT_TRUE(brillo::TouchFile(marker_path));
  ASSERT_TRUE(base::WriteFile(marker_path, marker_contents));

  std::optional<bool> result = hardware_.IsPowerwashScheduledByUpdateEngine();

  EXPECT_TRUE(result);
  EXPECT_TRUE(result.value());
}

TEST_F(HardwareChromeOSTest, IsPowerwashScheduledByUpdateEngineNoMarker) {
  base::FilePath root_path = root_dir_.GetPath();
  hardware_.SetRootForTest(root_path);
  base::FilePath marker_path = hardware_.GetPowerwashMarkerFullPath();
  ASSERT_FALSE(base::PathExists(marker_path));

  std::optional<bool> result = hardware_.IsPowerwashScheduledByUpdateEngine();

  EXPECT_FALSE(result);
}

TEST_F(HardwareChromeOSTest, IsPowerwashScheduledByUpdateEngineNoReasonKey) {
  base::FilePath root_path = root_dir_.GetPath();
  hardware_.SetRootForTest(root_path);
  base::FilePath marker_path = hardware_.GetPowerwashMarkerFullPath();
  std::string marker_contents = "safe reason test_key";
  ASSERT_TRUE(brillo::TouchFile(marker_path));
  ASSERT_TRUE(base::WriteFile(marker_path, marker_contents));

  std::optional<bool> result = hardware_.IsPowerwashScheduledByUpdateEngine();

  EXPECT_TRUE(result);
  EXPECT_FALSE(result.value());
}

TEST_F(HardwareChromeOSTest, IsPowerwashScheduledByUpdateEngineEmptyReason) {
  base::FilePath root_path = root_dir_.GetPath();
  hardware_.SetRootForTest(root_path);
  base::FilePath marker_path = hardware_.GetPowerwashMarkerFullPath();
  std::string marker_contents = "safe reason=\n test_key";
  ASSERT_TRUE(brillo::TouchFile(marker_path));
  ASSERT_TRUE(base::WriteFile(marker_path, marker_contents));

  std::optional<bool> result = hardware_.IsPowerwashScheduledByUpdateEngine();

  EXPECT_TRUE(result);
  EXPECT_FALSE(result.value());
}

}  // namespace chromeos_update_engine
