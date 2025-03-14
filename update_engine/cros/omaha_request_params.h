// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_CROS_OMAHA_REQUEST_PARAMS_H_
#define UPDATE_ENGINE_CROS_OMAHA_REQUEST_PARAMS_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/time/time.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "update_engine/common/constants.h"
#include "update_engine/common/platform_constants.h"
#include "update_engine/common/prefs.h"
#include "update_engine/cros/image_properties.h"
#include "update_engine/update_manager/update_check_allowed_policy.h"

// This gathers local system information and prepares info used by the
// Omaha request action.

namespace chromeos_update_engine {

extern const char kNoVersion[];
extern const char kMiniOsAppIdSuffix[];

// This class encapsulates the data Omaha gets for the request, along with
// essential state needed for the processing of the request/response.  The
// strings in this struct should not be XML escaped.
//
// TODO(jaysri): chromium-os:39752 tracks the need to rename this class to
// reflect its lifetime more appropriately.
class OmahaRequestParams {
 public:
  OmahaRequestParams() = default;
  OmahaRequestParams(const OmahaRequestParams&) = delete;
  OmahaRequestParams& operator=(const OmahaRequestParams&) = delete;

  virtual ~OmahaRequestParams();

  enum ActiveCountingType {
    kDayBased = 0,
    kDateBased,
  };

  struct AppParams {
    ActiveCountingType active_counting_type;
    // `critical_update` DLCs update with the OS, and will not be excluded if
    // encountered error.
    bool critical_update = false;
    // |name| is only used for DLCs to store the DLC ID.
    std::string name;
    int64_t ping_active;
    int64_t ping_date_last_active;
    int64_t ping_date_last_rollcall;
    bool send_ping;
    // |updated| is used for DLCs to decide sending DBus message to
    // dlcservice on an install/update completion.
    bool updated = true;
    // |last_fp| is used for DLCs to store the fingerprint value of previous
    // update.
    std::string last_fp;
  };

  struct MiniOSAppParam {
    // |updated| is used for MiniOS to keep track of whether the package was
    // installed or excluded.
    bool updated = true;
    // |last_fp| is used for MiniOS to store the fingerprint value of previous
    // update.
    std::string last_fp;
    // Version is used to store the MiniOS version, which is different from
    // platform.
    std::string version;
  };

  // Setters and getters for the various properties.
  inline std::string os_platform() const { return os_platform_; }
  inline std::string os_version() const { return os_version_; }
  inline std::string os_sp() const { return os_sp_; }
  inline std::string os_board() const { return image_props_.board; }
  inline std::string os_build_fingerprint() const {
    return image_props_.build_fingerprint;
  }
  inline std::string os_build_type() const { return image_props_.build_type; }
  inline std::string board_app_id() const { return image_props_.product_id; }
  inline std::string canary_app_id() const {
    return image_props_.canary_product_id;
  }
  inline void set_app_id(const std::string& app_id) {
    image_props_.product_id = app_id;
    image_props_.canary_product_id = app_id;
  }
  inline std::string hwid() const { return hwid_; }

  inline void set_app_version(const std::string& version) {
    image_props_.version = version;
  }
  inline std::string app_version() const { return image_props_.version; }
  inline std::string product_components() const {
    return image_props_.product_components;
  }
  inline void set_product_components(const std::string& product_components) {
    image_props_.product_components = product_components;
  }

  inline std::string current_channel() const {
    return image_props_.current_channel;
  }
  inline std::string target_channel() const {
    return mutable_image_props_.target_channel;
  }
  inline std::string download_channel() const { return download_channel_; }

  // Can client accept a delta ?
  inline void set_delta_okay(bool ok) { delta_okay_ = ok; }
  inline bool delta_okay() const { return delta_okay_; }

  // True if this is a user-initiated update check.
  inline void set_interactive(bool interactive) { interactive_ = interactive; }
  inline bool interactive() const { return interactive_; }

  inline void set_update_url(const std::string& url) { update_url_ = url; }
  inline std::string update_url() const { return update_url_; }

  inline void set_target_version_prefix(const std::string& prefix) {
    target_version_prefix_ = prefix;
  }

  inline std::string target_version_prefix() const {
    return target_version_prefix_;
  }

  inline std::string release_lts_tag() const { return release_lts_tag_; }
  inline void set_release_lts_tag(const std::string& tag) {
    release_lts_tag_ = tag;
  }

  inline std::string last_fp() const { return last_fp_; }

  inline void set_last_fp(const std::string& last_fp) { last_fp_ = last_fp; }

  inline void set_rollback_allowed(bool rollback_allowed) {
    rollback_allowed_ = rollback_allowed;
  }

  inline bool rollback_allowed() const { return rollback_allowed_; }

  inline void set_rollback_data_save_requested(
      bool rollback_data_save_requested) {
    rollback_data_save_requested_ = rollback_data_save_requested;
  }

  inline bool rollback_data_save_requested() const {
    return rollback_data_save_requested_;
  }

  inline void set_rollback_allowed_milestones(int rollback_allowed_milestones) {
    rollback_allowed_milestones_ = rollback_allowed_milestones;
  }

  inline int rollback_allowed_milestones() const {
    return rollback_allowed_milestones_;
  }

  inline void set_activate_date(const std::string& activate_date) {
    activate_date_ = activate_date;
  }

  inline std::string activate_date() const { return activate_date_; }

  inline void set_fsi_version(const std::string& fsi_version) {
    fsi_version_ = fsi_version;
  }

  inline std::string fsi_version() const { return fsi_version_; }

  inline void set_managed_device_in_oobe(bool managed_device_in_oobe) {
    managed_device_in_oobe_ = managed_device_in_oobe;
  }

  inline bool managed_device_in_oobe() const { return managed_device_in_oobe_; }

  inline void set_wall_clock_based_wait_enabled(bool enabled) {
    wall_clock_based_wait_enabled_ = enabled;
  }
  inline bool wall_clock_based_wait_enabled() const {
    return wall_clock_based_wait_enabled_;
  }

  inline void set_waiting_period(base::TimeDelta period) {
    waiting_period_ = period;
  }
  base::TimeDelta waiting_period() const { return waiting_period_; }

  inline void set_update_check_count_wait_enabled(bool enabled) {
    update_check_count_wait_enabled_ = enabled;
  }

  inline bool update_check_count_wait_enabled() const {
    return update_check_count_wait_enabled_;
  }

  inline void set_min_update_checks_needed(int64_t min) {
    min_update_checks_needed_ = min;
  }
  inline int64_t min_update_checks_needed() const {
    return min_update_checks_needed_;
  }

  inline void set_max_update_checks_allowed(int64_t max) {
    max_update_checks_allowed_ = max;
  }
  inline int64_t max_update_checks_allowed() const {
    return max_update_checks_allowed_;
  }
  inline void set_dlc_apps_params(
      const std::map<std::string, AppParams>& dlc_apps_params) {
    dlc_apps_params_ = dlc_apps_params;
  }
  inline const std::map<std::string, AppParams>& dlc_apps_params() const {
    return dlc_apps_params_;
  }

  inline const MiniOSAppParam& minios_app_params() const {
    return minios_app_params_;
  }

  inline void set_minios_app_params(MiniOSAppParam minios_app_params) {
    minios_app_params_ = minios_app_params;
  }

  inline void set_is_install(bool is_install) { is_install_ = is_install; }
  inline bool is_install() const { return is_install_; }

  inline void set_quick_fix_build_token(const std::string& token) {
    quick_fix_build_token_ = token;
  }
  inline const std::string& quick_fix_build_token() const {
    return quick_fix_build_token_;
  }

  inline void set_market_segment(const std::string& market_segment) {
    market_segment_ = market_segment;
  }
  inline const std::string& market_segment() const { return market_segment_; }

  inline void set_hw_details(bool hw_details) { hw_details_ = hw_details; }
  inline bool hw_details() const { return hw_details_; }

  inline void set_extended_okay(bool okay) { extended_okay_ = okay; }
  inline bool extended_okay() const { return extended_okay_; }

  // Returns the App ID corresponding to the current value of the
  // download channel.
  virtual std::string GetAppId() const;

  // Returns the DLC app ID.
  virtual std::string GetDlcAppId(const std::string& dlc_id) const;

  // Returns true if the App ID is a DLC App ID that is currently part of the
  // request parameters.
  virtual bool IsDlcAppId(const std::string& app_id) const;

  // Returns the DLC App ID if the given App ID is a DLC that is currently part
  // of the request parameters.
  virtual bool GetDlcId(const std::string& app_id, std::string* dlc_id) const;

  // If the App ID is a DLC App ID will set to no update.
  void SetDlcNoUpdate(const std::string& app_id);

  // Returns true if the App ID is a MiniOS App ID.
  bool IsMiniOSAppId(const std::string& app_id) const;

  // Set `minios_app_params` update field.
  void SetMiniOSUpdate(bool updated);

  // Suggested defaults
  static const char kOsVersion[];
  static const int64_t kDefaultMinUpdateChecks = 0;
  static const int64_t kDefaultMaxUpdateChecks = 8;

  // Initializes all the data in the object. Non-empty
  // |in_app_version| or |in_update_url| prevents automatic detection
  // of the parameter. Returns true on success, false otherwise.
  bool Init(const std::string& in_app_version,
            const std::string& in_update_url,
            const chromeos_update_manager::UpdateCheckParams& params);

  // Permanently changes the release channel to |channel|. Performs a
  // powerwash, if required and allowed.
  // Returns true on success, false otherwise. Note: This call will fail if
  // there's a channel change pending already. This is to serialize all the
  // channel changes done by the user in order to avoid having to solve
  // numerous edge cases around ensuring the powerwash happens as intended in
  // all such cases.
  virtual bool SetTargetChannel(const std::string& channel,
                                bool is_powerwash_allowed,
                                std::string* error_message);

  // Updates the download channel for this particular attempt from the current
  // value of target channel.  This method takes a "snapshot" of the current
  // value of target channel and uses it for all subsequent Omaha requests for
  // this attempt (i.e. initial request as well as download progress/error
  // event requests). The snapshot will be updated only when either this method
  // or Init is called again.
  virtual void UpdateDownloadChannel();

  // Returns whether we should powerwash for this update. Note that this is
  // just an indication, the final decision to powerwash or not is made in the
  // response handler.
  bool ShouldPowerwash() const;

  // Check if the provided update URL is official, meaning either the default
  // autoupdate server or the autoupdate autotest server.
  virtual bool IsUpdateUrlOfficial() const;

  // IsCommercialChannel returns true if `channel` is a channel only supported
  // on enrolled devices.
  static bool IsCommercialChannel(const std::string& channel);

  // For unit-tests.
  void set_root(const std::string& root);
  void set_current_channel(const std::string& channel) {
    image_props_.current_channel = channel;
  }
  void set_target_channel(const std::string& channel) {
    mutable_image_props_.target_channel = channel;
  }
  void set_os_sp(const std::string& os_sp) { os_sp_ = os_sp; }
  void set_os_board(const std::string& os_board) {
    image_props_.board = os_board;
  }
  void set_hwid(const std::string& hwid) { hwid_ = hwid; }
  void set_is_powerwash_allowed(bool powerwash_allowed) {
    mutable_image_props_.is_powerwash_allowed = powerwash_allowed;
  }
  bool is_powerwash_allowed() {
    return mutable_image_props_.is_powerwash_allowed;
  }

 private:
  FRIEND_TEST(OmahaRequestParamsTest, ChannelIndexTest);
  FRIEND_TEST(OmahaRequestParamsTest, IsValidChannelTest);
  FRIEND_TEST(OmahaRequestParamsTest, SetIsPowerwashAllowedTest);
  FRIEND_TEST(OmahaRequestParamsTest, SetTargetChannelInvalidTest);
  FRIEND_TEST(OmahaRequestParamsTest, SetTargetChannelTest);
  FRIEND_TEST(OmahaRequestParamsTest, ShouldPowerwashTest);
  FRIEND_TEST(OmahaRequestParamsTest, ToMoreStableChannelFlagTest);

  // Returns true if |channel| is a valid channel, otherwise write error to
  // |error_message| if passed and return false.
  bool IsValidChannel(const std::string& channel,
                      std::string* error_message) const;
  bool IsValidChannel(const std::string& channel) const {
    return IsValidChannel(channel, nullptr);
  }

  // Returns the index of the given channel.
  int GetChannelIndex(const std::string& channel) const;

  // True if we're trying to update to a more stable channel.
  // i.e. index(target_channel) > index(current_channel).
  bool ToMoreStableChannel() const;

  // Gets the machine type (e.g. "i686").
  std::string GetMachineType() const;

  // The system image properties.
  ImageProperties image_props_;
  MutableImageProperties mutable_image_props_;

  // Basic properties of the OS and Application that go into the Omaha request.
  std::string os_platform_ = constants::kOmahaPlatformName;
  std::string os_version_ = kOsVersion;
  std::string os_sp_;

  // There are three channel values we deal with:
  // * The channel we got the image we are running from or "current channel"
  //   stored in |image_props_.current_channel|.
  //
  // * The release channel we are tracking, where we should get updates from,
  //   stored in |mutable_image_props_.target_channel|. This channel is
  //   normally the same as the current_channel, except when the user changes
  //   the channel. In that case it'll have the release channel the user
  //   switched to, regardless of whether we downloaded an update from that
  //   channel or not, or if we are in the middle of a download from a
  //   previously selected channel  (as opposed to download channel
  //   which gets updated only at the start of next download).
  //
  // * The channel from which we're downloading the payload. This should
  //   normally be the same as target channel. But if the user made another
  //   channel change after we started the download, then they'd be different,
  //   in which case, we'd detect elsewhere that the target channel has been
  //   changed and cancel the current download attempt.
  std::string download_channel_;

  // The value defining the OS fingerprint of the previous update. Empty
  // otherwise.
  std::string last_fp_;

  // The value defining the parameters of the LTS (Long Term Support).
  // Normally is set by |OmahaRequestParamsPolicy|.
  std::string release_lts_tag_;

  std::string hwid_;          // Hardware Qualification ID of the client
  bool delta_okay_ = true;    // If this client can accept a delta
  bool interactive_ = false;  // Whether this is a user-initiated update check

  // The URL to send the Omaha request to.
  std::string update_url_;

  // Prefix of the target OS version that the enterprise wants this device
  // to be pinned to. It's empty otherwise.
  std::string target_version_prefix_;

  // Whether the client is accepting rollback images defined by policy.
  // Normally ss set by |OmahaRequestParamsPolicy|.
  bool rollback_allowed_ = false;

  // Whether rollbacks should preserve some system state during powerwash.
  // Normally ss set by |OmahaRequestParamsPolicy|.
  bool rollback_data_save_requested_ = false;

  // Specifies the number of Chrome milestones rollback should be allowed,
  // starting from the stable version at any time. Value is -1 if unspecified
  // (e.g. no device policy is available yet), in this case no version
  // roll-forward should happen.
  // Normally ss set by |OmahaRequestParamsPolicy|.
  int rollback_allowed_milestones_;

  // FSI OS version of this device, as read from VPD.
  std::string fsi_version_;

  // Activate date in the form of "2023-04" of this device, as read from VPD.
  std::string activate_date_;

  // True if scattering or staging are enabled, in which case waiting_period_
  // specifies the amount of absolute time that we've to wait for before sending
  // a request to Omaha.
  bool wall_clock_based_wait_enabled_ = false;
  base::TimeDelta waiting_period_;

  // True if scattering or staging are enabled to denote the number of update
  // checks we've to skip before we can send a request to Omaha. The min and max
  // values establish the bounds for a random number to be chosen within that
  // range to enable such a wait.
  bool update_check_count_wait_enabled_ = false;
  int64_t min_update_checks_needed_ = kDefaultMinUpdateChecks;
  int64_t max_update_checks_allowed_ = kDefaultMaxUpdateChecks;

  // When reading files, prepend root_ to the paths. Useful for testing.
  std::string root_;

  // A list of DLC modules to install. A mapping from DLC App ID to |AppParams|.
  std::map<std::string, AppParams> dlc_apps_params_;

  MiniOSAppParam minios_app_params_;

  // This variable defines whether the payload is being installed in the current
  // partition. At the moment, this is used for installing DLC modules on the
  // current active partition instead of the inactive partition.
  bool is_install_ = false;

  // Token used when making an update request for a specific build.
  // For example: Token for a Quick Fix Build:
  // https://cloud.google.com/docs/chrome-enterprise/policies/?policy=DeviceQuickFixBuildToken
  // Normally is set by |OmahaRequestParamsPolicy|.
  std::string quick_fix_build_token_;

  // Defines the device's market segment.
  std::string market_segment_;

  // Whether the device is in OOBE and was managed before being reset.
  bool managed_device_in_oobe_ = false;

  // Determine if extended auto updates are okay.
  bool extended_okay_ = false;

  // Whether to include <hw> element.
#if USE_HW_DETAILS
  bool hw_details_ = true;
#else
  bool hw_details_ = false;
#endif  // USE_HW_DETAILS
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_CROS_OMAHA_REQUEST_PARAMS_H_
