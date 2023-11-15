//
// Copyright (C) 2013 The Android Open Source Project
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

#ifndef UPDATE_ENGINE_CROS_HARDWARE_CHROMEOS_H_
#define UPDATE_ENGINE_CROS_HARDWARE_CHROMEOS_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/time/time.h>
#include <debugd/dbus-proxies.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST
#include <libcrossystem/crossystem.h>

#include "update_engine/common/error_code.h"
#include "update_engine/common/hardware_interface.h"

namespace chromeos_update_engine {

// Implements the real interface with Chrome OS verified boot and recovery
// process.
class HardwareChromeOS final : public HardwareInterface {
 public:
  HardwareChromeOS();
  HardwareChromeOS(const HardwareChromeOS&) = delete;
  HardwareChromeOS& operator=(const HardwareChromeOS&) = delete;

  ~HardwareChromeOS() override = default;

  void Init();

  // HardwareInterface methods.
  bool IsOfficialBuild() const override;
  bool IsNormalBootMode() const override;
  bool IsRunningFromMiniOs() const override;
  bool AreDevFeaturesEnabled() const override;
  bool IsOOBEEnabled() const override;
  bool IsOOBEComplete(base::Time* out_time_of_oobe) const override;
  std::string GetHardwareClass() const override;
  std::string GetDeviceRequisition() const override;
  std::unique_ptr<base::Value> ReadLocalState() const override;
  bool IsEnrollmentRecoveryModeEnabled(
      const base::Value* local_state) const override;
  bool IsConsumerSegmentSet(const base::Value* local_state) const override;
  int GetMinKernelKeyVersion() const override;
  int GetMinFirmwareKeyVersion() const override;
  int GetMaxFirmwareKeyRollforward() const override;
  bool SetMaxFirmwareKeyRollforward(int firmware_max_rollforward) override;
  bool SetMaxKernelKeyRollforward(int kernel_max_rollforward) override;
  int GetPowerwashCount() const override;
  std::optional<bool> IsPowerwashScheduledByUpdateEngine() const override;
  // Must not be called prior to boot control initialization.
  bool SchedulePowerwash(bool save_rollback_data) override;
  bool CancelPowerwash() override;
  bool GetNonVolatileDirectory(base::FilePath* path) const override;
  bool GetRecoveryKeyVersion(std::string* version) override;
  bool GetPowerwashSafeDirectory(base::FilePath* path) const override;
  int64_t GetBuildTimestamp() const override;
  bool AllowDowngrade() const override { return false; }
  bool GetFirstActiveOmahaPingSent() const override;
  bool SetFirstActiveOmahaPingSent() override;
  std::string GetActivateDate() const override;
  std::string GetFsiVersion() const override;
  bool GetCheckEnrollment() const override;
  int GetActiveMiniOsPartition() const override;
  bool SetActiveMiniOsPartition(int active_partition) override;
  void SetWarmReset(bool warm_reset) override;
  std::string GetVersionForLogging(
      const std::string& partition_name) const override;
  ErrorCode IsPartitionUpdateValid(
      const std::string& partition_name,
      const std::string& new_version) const override;
  bool IsRootfsVerificationEnabled() const override;

  void SetRootForTest(base::FilePath test_root) { root_ = test_root; }
  void SetNonVolatileDirectoryForTest(const base::FilePath& path) {
    non_volatile_path_ = path;
  }

  bool ResetFWTryNextSlot() override;

  base::FilePath GetPowerwashMarkerFullPath() const override;

  bool IsManagedDeviceInOobe() const override;

 private:
  friend class HardwareChromeOSTest;
  FRIEND_TEST(HardwareChromeOSTest, GeneratePowerwashCommandCheck);
  FRIEND_TEST(HardwareChromeOSTest,
              GeneratePowerwashCommandWithRollbackDataCheck);
  FRIEND_TEST(HardwareChromeOSFWTest,
              ResetsFWTryNextSlotProperlyIfValidMainFwAct);
  FRIEND_TEST(HardwareChromeOSTest, ResetFWTryNextSlotFailsIfInvalidMainFwAct);
  FRIEND_TEST(HardwareChromeOSTest, ResetFWTryNextSlotFailsIfMissingMainFwAct);
  FRIEND_TEST(HardwareChromeOSTest,
              ResetFWTryNextSlotFailsIfSettingResultFlagFails);
  FRIEND_TEST(HardwareChromeOSTest,
              ResetFWTryNextSlotFailsIfSettingTryCountFails);

  // Returns a currently active firmware slot.
  // `kFWSlotA` or `kFWSlotB` most of the time, though can be "recovery" or
  // "error".
  std::optional<std::string> GetMainFWAct() const;
  // Sets a RW firmware partition slot to try on next boot to
  // |target_slot|.
  // Expects only `kFWSlotA` or `kFWSlotB` as |target_slot|.
  // Returns false on failure.
  bool SetFWTryNextSlot(base::StringPiece target_slot);
  // Marks current RW firmware boot result as success.
  // Returns false on failure.
  bool SetFWResultSuccessful();
  // Sets a number of times to try a next boot RW partition slot to |count|.
  // Returns false on failure.
  bool SetFWTryCount(int count);

  // Load the update manager config flags (is_oobe_enabled flag) from the
  // appropriate location based on whether we are in a normal mode boot (as
  // passed in |normal_mode|) prefixing the paths with |root_prefix|.
  void LoadConfig(const std::string& root_prefix, bool normal_mode);

  std::string GeneratePowerwashCommand(bool save_rollback_data) const;

  bool is_oobe_enabled_;

  std::string recovery_key_version_;

  base::FilePath root_;
  base::FilePath non_volatile_path_;

  std::unique_ptr<org::chromium::debugdProxyInterface> debugd_proxy_;
  std::unique_ptr<crossystem::Crossystem> crossystem_;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_CROS_HARDWARE_CHROMEOS_H_
