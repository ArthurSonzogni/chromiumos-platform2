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

#ifndef UPDATE_ENGINE_COMMON_FAKE_HARDWARE_H_
#define UPDATE_ENGINE_COMMON_FAKE_HARDWARE_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/json/json_string_value_serializer.h>
#include <base/time/time.h>

#include "update_engine/common/error_code.h"
#include "update_engine/common/hardware_interface.h"
#include "update_engine/common/utils.h"

namespace chromeos_update_engine {

// Implements a fake hardware interface used for testing.
class FakeHardware : public HardwareInterface {
 public:
  // Value used to signal that the powerwash_count file is not present. When
  // this value is used in SetPowerwashCount(), GetPowerwashCount() will return
  // false.
  static const int kPowerwashCountNotSet = -1;

  // Default value for crossystem tpm_kernver.
  static const int kMinKernelKeyVersion = 3;

  // Default value for crossystem tpm_fwver.
  static const int kMinFirmwareKeyVersion = 13;

  // Default value for crossystem kernel_max_rollforward. This value is the
  // default for consumer devices and effectively means "unlimited rollforward
  // is allowed", which is the same as the behavior prior to implementing
  // roll forward prevention.
  static const int kKernelMaxRollforward = 0xfffffffe;

  // Default value for crossystem firmware_max_rollforward. This value is the
  // default for consumer devices and effectively means "unlimited rollforward
  // is allowed", which is the same as the behavior prior to implementing
  // roll forward prevention.
  static const int kFirmwareMaxRollforward = 0xfffffffe;

  FakeHardware() = default;
  FakeHardware(const FakeHardware&) = delete;
  FakeHardware& operator=(const FakeHardware&) = delete;

  // HardwareInterface methods.
  bool IsOfficialBuild() const override { return is_official_build_; }

  bool IsNormalBootMode() const override { return is_normal_boot_mode_; }

  bool IsRunningFromMiniOs() const override { return is_running_from_minios_; }

  bool AreDevFeaturesEnabled() const override {
    return are_dev_features_enabled_;
  }

  bool IsOOBEEnabled() const override { return is_oobe_enabled_; }

  bool IsOOBEComplete(base::Time* out_time_of_oobe) const override {
    if (out_time_of_oobe != nullptr)
      *out_time_of_oobe = oobe_timestamp_;
    return is_oobe_complete_;
  }

  std::string GetHardwareClass() const override { return hardware_class_; }

  std::string GetDeviceRequisition() const override {
    return device_requisition_;
  }

  int GetMinKernelKeyVersion() const override {
    return min_kernel_key_version_;
  }

  int GetMinFirmwareKeyVersion() const override {
    return min_firmware_key_version_;
  }

  int GetMaxFirmwareKeyRollforward() const override {
    return firmware_max_rollforward_;
  }

  bool SetMaxFirmwareKeyRollforward(int firmware_max_rollforward) override {
    if (GetMaxFirmwareKeyRollforward() == -1)
      return false;

    firmware_max_rollforward_ = firmware_max_rollforward;
    return true;
  }

  bool IsEnrollmentRecoveryModeEnabled(
      const base::Value* local_state) const override {
    return is_enrollment_recovery_enabled_;
  }

  bool IsConsumerSegmentSet(const base::Value* local_state) const override {
    return is_consumer_segment_;
  }

  std::unique_ptr<base::Value> ReadLocalState() const override {
    int error_code;
    std::string error_msg;

    JSONStringValueDeserializer deserializer(local_state_contents_);

    return deserializer.Deserialize(&error_code, &error_msg);
  }

  bool SetMaxKernelKeyRollforward(int kernel_max_rollforward) override {
    kernel_max_rollforward_ = kernel_max_rollforward;
    return true;
  }

  int GetPowerwashCount() const override { return powerwash_count_; }

  bool SchedulePowerwash(bool save_rollback_data) override {
    powerwash_scheduled_ = true;
    save_rollback_data_ = save_rollback_data;
    return true;
  }

  bool CancelPowerwash() override {
    powerwash_scheduled_ = false;
    save_rollback_data_ = false;
    return true;
  }

  bool IsPowerwashScheduled() { return powerwash_scheduled_; }

  bool GetNonVolatileDirectory(base::FilePath* path) const override {
    return false;
  }

  bool GetRecoveryKeyVersion(std::string* version) override {
    if (recovery_key_version_.empty()) {
      return false;
    }
    *version = recovery_key_version_;
    return true;
  }

  bool GetPowerwashSafeDirectory(base::FilePath* path) const override {
    return false;
  }

  int64_t GetBuildTimestamp() const override { return build_timestamp_; }

  bool AllowDowngrade() const override { return false; }

  bool GetFirstActiveOmahaPingSent() const override {
    return first_active_omaha_ping_sent_;
  }

  bool SetFirstActiveOmahaPingSent() override {
    first_active_omaha_ping_sent_ = true;
    return true;
  }

  std::string GetActivateDate() const override { return activate_date_; }

  std::string GetFsiVersion() const override { return fsi_version_; }

  bool GetCheckEnrollment() const override { return check_enrollment_; }

  int GetActiveMiniOsPartition() const override { return 0; }

  bool SetActiveMiniOsPartition(int active_partition) override { return true; }

  // Setters
  void SetIsOfficialBuild(bool is_official_build) {
    is_official_build_ = is_official_build;
  }

  void SetIsNormalBootMode(bool is_normal_boot_mode) {
    is_normal_boot_mode_ = is_normal_boot_mode;
  }

  void SetIsRunningFromMiniOs(bool is_running_from_minios) {
    is_running_from_minios_ = is_running_from_minios;
  }

  void SetAreDevFeaturesEnabled(bool are_dev_features_enabled) {
    are_dev_features_enabled_ = are_dev_features_enabled;
  }

  // Sets the SetIsOOBEEnabled to |is_oobe_enabled|.
  void SetIsOOBEEnabled(bool is_oobe_enabled) {
    is_oobe_enabled_ = is_oobe_enabled;
  }

  // Sets the IsOOBEComplete to True with the given timestamp.
  void SetIsOOBEComplete(base::Time oobe_timestamp) {
    is_oobe_complete_ = true;
    oobe_timestamp_ = oobe_timestamp;
  }

  void UnsetIsOOBEComplete() { is_oobe_complete_ = false; }

  void SetIsEnrollmentRecoveryMode(bool enrollment_recovery) {
    is_enrollment_recovery_enabled_ = enrollment_recovery;
  }

  void SetIsConsumerSegment(bool consumer_segment) {
    is_consumer_segment_ = consumer_segment;
  }

  void SetLocalState(std::string local_state) {
    local_state_contents_ = local_state;
  }

  void SetHardwareClass(const std::string& hardware_class) {
    hardware_class_ = hardware_class;
  }

  void SetDeviceRequisition(const std::string& requisition) {
    device_requisition_ = requisition;
  }

  void SetMinKernelKeyVersion(int min_kernel_key_version) {
    min_kernel_key_version_ = min_kernel_key_version;
  }

  void SetMinFirmwareKeyVersion(int min_firmware_key_version) {
    min_firmware_key_version_ = min_firmware_key_version;
  }

  void SetPowerwashCount(int powerwash_count) {
    powerwash_count_ = powerwash_count;
  }

  void SetBuildTimestamp(int64_t build_timestamp) {
    build_timestamp_ = build_timestamp;
  }

  void SetWarmReset(bool warm_reset) override { warm_reset_ = warm_reset; }

  void SetRecoveryKeyVersion(const std::string& version) {
    recovery_key_version_ = version;
  }

  void SetActivateDate(const std::string& activate_date) {
    activate_date_ = activate_date;
  }

  void SetFsiVersion(const std::string& fsi_version) {
    fsi_version_ = fsi_version;
  }

  void SetCheckEnrollment(bool check_enrollment) {
    check_enrollment_ = check_enrollment;
  }

  // Getters to verify state.
  int GetMaxKernelKeyRollforward() const { return kernel_max_rollforward_; }

  bool GetIsRollbackPowerwashScheduled() const {
    return powerwash_scheduled_ && save_rollback_data_;
  }
  std::string GetVersionForLogging(
      const std::string& partition_name) const override {
    return partition_timestamps_[partition_name];
  }
  void SetVersion(const std::string& partition_name, std::string timestamp) {
    partition_timestamps_[partition_name] = std::move(timestamp);
  }
  ErrorCode IsPartitionUpdateValid(
      const std::string& partition_name,
      const std::string& new_version) const override {
    const auto old_version = GetVersionForLogging(partition_name);
    return utils::IsTimestampNewer(old_version, new_version);
  }

  bool IsRootfsVerificationEnabled() const override {
    return rootfs_verification_enabled_;
  }

  bool ResetFWTryNextSlot() override {
    if (fail_reset_fw_try_next_slot_) {
      return false;
    }

    return reset_fw_try_next_slot_ = true;
  }

  void SetFailResetFwTryNextSlot(bool value) {
    fail_reset_fw_try_next_slot_ = value;
  }
  bool IsFWTryNextSlotReset() const { return reset_fw_try_next_slot_; }

  std::optional<bool> IsPowerwashScheduledByUpdateEngine() const override {
    return is_powerwash_scheduled_by_update_engine_;
  }
  void SetIsPowerwashScheduledByUpdateEngine(std::optional<bool> value) {
    is_powerwash_scheduled_by_update_engine_ = value;
  }

  base::FilePath GetPowerwashMarkerFullPath() const override {
    return base::FilePath();
  };

  bool IsManagedDeviceInOobe() const override {
    return managed_device_in_oobe_;
  }
  void SetManagedDeviceInOobe(bool managed_device_in_oobe) {
    managed_device_in_oobe_ = managed_device_in_oobe;
  }

 private:
  bool is_official_build_{true};
  bool is_normal_boot_mode_{true};
  bool is_running_from_minios_{false};
  bool are_dev_features_enabled_{false};
  bool is_oobe_enabled_{true};
  bool is_oobe_complete_{true};
  bool is_enrollment_recovery_enabled_{false};
  bool is_consumer_segment_{false};
  std::string local_state_contents_;
  // Jan 20, 2007
  base::Time oobe_timestamp_{base::Time::FromTimeT(1169280000)};
  std::string hardware_class_{"Fake HWID BLAH-1234"};
  std::string device_requisition_{"fake_requisition"};
  int min_kernel_key_version_{kMinKernelKeyVersion};
  int min_firmware_key_version_{kMinFirmwareKeyVersion};
  int kernel_max_rollforward_{kKernelMaxRollforward};
  int firmware_max_rollforward_{kFirmwareMaxRollforward};
  int powerwash_count_{kPowerwashCountNotSet};
  std::optional<bool> is_powerwash_scheduled_by_update_engine_{true};
  bool powerwash_scheduled_{false};
  bool save_rollback_data_{false};
  int64_t build_timestamp_{0};
  bool first_active_omaha_ping_sent_{false};
  std::string activate_date_{""};
  std::string fsi_version_{""};
  bool check_enrollment_{false};
  bool warm_reset_{false};
  std::string recovery_key_version_;
  mutable std::map<std::string, std::string> partition_timestamps_;
  bool rootfs_verification_enabled_{false};
  bool reset_fw_try_next_slot_{false};
  bool fail_reset_fw_try_next_slot_{false};
  bool managed_device_in_oobe_{false};
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_FAKE_HARDWARE_H_
