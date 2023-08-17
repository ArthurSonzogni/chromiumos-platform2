// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_POLICY_DEVICE_POLICY_IMPL_H_
#define LIBBRILLO_POLICY_DEVICE_POLICY_IMPL_H_

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>

#include "install_attributes/libinstallattributes.h"
#include "policy/device_policy.h"

// Avoid transitive includes of protofile headers. See b:294577904.
namespace enterprise_management {
class PolicyFetchResponse;
class PolicyData;
class ChromeDeviceSettingsProto;
}  // namespace enterprise_management

#pragma GCC visibility push(default)

namespace policy {

// This class holds device settings that are to be enforced across all users.
//
// Before serving it to the users this class verifies that the policy is valid
// against its signature and the owner's key and also that the policy files
// are owned by root.
class DevicePolicyImpl : public DevicePolicy {
 public:
  DevicePolicyImpl();
  DevicePolicyImpl(const DevicePolicyImpl&) = delete;
  DevicePolicyImpl& operator=(const DevicePolicyImpl&) = delete;

  ~DevicePolicyImpl() override;

  const enterprise_management::PolicyFetchResponse& get_policy_fetch_response()
      const {
    return *policy_;
  }

  int get_number_of_policy_files() const { return number_of_policy_files_; }

  int get_number_of_invalid_files() const { return number_of_invalid_files_; }

  // Loads the device policy. The |delete| argument specifies whether to delete
  // the device policy files that failed to load or pass validation.
  bool LoadPolicyDeleteInvalid(bool delete_invalid_files);

  // DevicePolicy overrides:
  bool LoadPolicy(bool delete_invalid_files) override;
  bool IsEnterpriseEnrolled() const override;
  bool GetPolicyRefreshRate(int* rate) const override;
  bool GetGuestModeEnabled(bool* guest_mode_enabled) const override;
  bool GetCameraEnabled(bool* camera_enabled) const override;
  bool GetShowUserNames(bool* show_user_names) const override;
  bool GetDataRoamingEnabled(bool* data_roaming_enabled) const override;
  bool GetAllowNewUsers(bool* allow_new_users) const override;
  bool GetMetricsEnabled(bool* metrics_enabled) const override;
  bool GetHwDataUsageEnabled(bool* hw_data_usage_enabled) const override;
  bool GetReportSystemInfo(bool* report_system_info) const override;
  bool GetReportCpuInfo(bool* report_cpu_info) const override;
  bool GetReportGraphicsStatus(bool* report_graphics_status) const override;
  bool GetReportMemoryInfo(bool* report_memory_info) const override;
  bool GetReportNetworkConfig(bool* report_network_config) const override;
  bool GetReportVersionInfo(bool* report_version_info) const override;
  bool GetReportActivityTimes(bool* report_activity_times) const override;
  bool GetReportBootMode(bool* report_boot_mode) const override;
  bool GetEphemeralSettings(
      EphemeralSettings* ephemeral_settings) const override;
  bool GetReleaseChannel(std::string* release_channel) const override;
  bool GetReleaseChannelDelegated(
      bool* release_channel_delegated) const override;
  bool GetReleaseLtsTag(std::string* lts_tag) const override;
  bool GetUpdateDisabled(bool* update_disabled) const override;
  bool GetTargetVersionPrefix(
      std::string* target_version_prefix) const override;
  bool GetRollbackToTargetVersion(
      int* rollback_to_target_version) const override;
  bool GetRollbackAllowedMilestones(
      int* rollback_allowed_milestones) const override;
  bool GetScatterFactorInSeconds(
      int64_t* scatter_factor_in_seconds) const override;
  bool GetAllowedConnectionTypesForUpdate(
      std::set<std::string>* connection_types) const override;
  bool GetOpenNetworkConfiguration(
      std::string* open_network_configuration) const override;
  bool GetOwner(std::string* owner) const override;
  bool GetHttpDownloadsEnabled(bool* http_downloads_enabled) const override;
  bool GetAuP2PEnabled(bool* au_p2p_enabled) const override;
  bool GetAllowKioskAppControlChromeVersion(
      bool* allow_kiosk_app_control_chrome_version) const override;
  bool GetUsbDetachableWhitelist(
      std::vector<UsbDeviceId>* usb_whitelist) const override;
  bool GetAutoLaunchedKioskAppId(std::string* app_id_out) const override;
  bool IsEnterpriseManaged() const override;
  bool GetSecondFactorAuthenticationMode(int* mode_out) const override;
  std::optional<bool> GetRunAutomaticCleanupOnLogin() const override;
  bool GetDisallowedTimeIntervals(
      std::vector<WeeklyTimeInterval>* intervals_out) const override;
  bool GetDeviceUpdateStagingSchedule(
      std::vector<DayPercentagePair>* staging_schedule_out) const override;
  bool GetDeviceQuickFixBuildToken(
      std::string* device_quick_fix_build_token) const override;
  bool GetDeviceDirectoryApiId(
      std::string* device_directory_api_out) const override;
  bool GetCustomerId(std::string* customer_id_out) const override;
  bool GetChannelDowngradeBehavior(
      int* channel_downgrade_behavior_out) const override;
  bool GetHighestDeviceMinimumVersion(
      base::Version* versions_out) const override;
  bool GetDeviceMarketSegment(
      DeviceMarketSegment* device_market_segment) const override;
  bool GetDeviceDebugPacketCaptureAllowed(bool* allowed) const override;
  bool GetDeviceKeylockerForStorageEncryptionEnabled(
      bool* keylocker_enabled) const override;
  std::optional<bool> GetReportDeviceSecurityStatus() const override;
  std::optional<bool> GetDeviceReportXDREvents() const override;

  // Methods that can be used only for testing.
  const enterprise_management::ChromeDeviceSettingsProto&
  get_device_policy_for_testing() const {
    return *device_policy_;
  }
  void set_policy_data_for_testing(
      const enterprise_management::PolicyData& policy_data);
  void set_verify_root_ownership_for_testing(bool verify_root_ownership);
  void set_install_attributes_for_testing(
      std::unique_ptr<InstallAttributesReader> install_attributes_reader);
  void set_policy_for_testing(
      const enterprise_management::ChromeDeviceSettingsProto& device_policy);
  void set_policy_path_for_testing(const base::FilePath& policy_path);
  void set_key_file_path_for_testing(const base::FilePath& keyfile_path);
  void set_verify_policy_for_testing(bool value);

 private:
  // Verifies that both the policy file and the signature file exist and are
  // owned by the root. Does nothing when |verify_root_ownership_| is set to
  // false.
  bool VerifyPolicyFile(const base::FilePath& policy_path);

  // Verifies that the policy signature is correct.
  bool VerifyPolicySignature() override;

  // Loads policy off of disk from |policy_path| into |policy_|. Returns true if
  // the |policy_path| is present on disk and loading it is successful.
  bool LoadPolicyFromFile(const base::FilePath& policy_path);

  // Path of the default policy file, e.g. /path/to/policy. In order to make
  // device policy more resilient against broken files, this class also tries to
  // load indexed paths /path/to/policy.1, /path/to/policy.2 etc., see
  // resilient_policy_utils.h.
  base::FilePath policy_path_;
  base::FilePath keyfile_path_;
  std::unique_ptr<InstallAttributesReader> install_attributes_reader_;
  std::unique_ptr<enterprise_management::PolicyFetchResponse> policy_;
  std::unique_ptr<enterprise_management::PolicyData> policy_data_;
  std::unique_ptr<enterprise_management::ChromeDeviceSettingsProto>
      device_policy_;

  // Total number of device policy files identified.
  int number_of_policy_files_ = 0;

  // Number of device policy files that have been detected as invalid.
  int number_of_invalid_files_ = 0;

  // If true, verify that policy files are owned by root. True in production
  // but can be set to false by tests.
  bool verify_root_ownership_ = true;
  // If false, all types of verification are disabled. True in production
  // but can be set to false by tests.
  bool verify_policy_ = true;
};
}  // namespace policy

#pragma GCC visibility pop

#endif  // LIBBRILLO_POLICY_DEVICE_POLICY_IMPL_H_
