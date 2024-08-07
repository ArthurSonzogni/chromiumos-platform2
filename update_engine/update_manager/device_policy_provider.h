// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_DEVICE_POLICY_PROVIDER_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_DEVICE_POLICY_PROVIDER_H_

#include <set>
#include <string>

#include <base/time/time.h>
#include <base/version.h>
#include <policy/libpolicy.h>

#include "update_engine/update_manager/provider.h"
#include "update_engine/update_manager/rollback_prefs.h"
#include "update_engine/update_manager/shill_provider.h"
#include "update_engine/update_manager/variable.h"
#include "update_engine/update_manager/weekly_time.h"

namespace chromeos_update_manager {

// Provides access to the current DevicePolicy.
class DevicePolicyProvider : public Provider {
 public:
  DevicePolicyProvider(const DevicePolicyProvider&) = delete;
  DevicePolicyProvider& operator=(const DevicePolicyProvider&) = delete;
  ~DevicePolicyProvider() override {}

  // Variable stating whether the DevicePolicy was loaded.
  virtual Variable<bool>* var_device_policy_is_loaded() = 0;

  // Variables mapping the information received on the DevicePolicy protobuf.
  virtual Variable<std::string>* var_release_channel() = 0;

  virtual Variable<bool>* var_release_channel_delegated() = 0;

  virtual Variable<std::string>* var_release_lts_tag() = 0;

  virtual Variable<bool>* var_update_disabled() = 0;

  virtual Variable<std::string>* var_target_version_prefix() = 0;

  // Variable returning what should happen if the target_version_prefix is
  // earlier than the current Chrome OS version.
  virtual Variable<RollbackToTargetVersion>*
  var_rollback_to_target_version() = 0;

  // Variable returning the number of Chrome milestones rollback should be
  // possible. Rollback protection will be postponed by this many versions.
  virtual Variable<int>* var_rollback_allowed_milestones() = 0;

  // Returns a non-negative scatter interval used for updates.
  virtual Variable<base::TimeDelta>* var_scatter_factor() = 0;

  // Variable returning the set of connection types allowed for updates. The
  // identifiers returned are consistent with the ones returned by the
  // ShillProvider.
  virtual Variable<std::set<chromeos_update_engine::ConnectionType>>*
  var_allowed_connection_types_for_update() = 0;

  // Variable stating whether the device has an owner. For enterprise enrolled
  // devices, this will be false as the device owner has an empty string.
  virtual Variable<bool>* var_has_owner() = 0;

  virtual Variable<bool>* var_http_downloads_enabled() = 0;

  virtual Variable<bool>* var_au_p2p_enabled() = 0;

  virtual Variable<bool>* var_allow_kiosk_app_control_chrome_version() = 0;

  // Variable that contains the time intervals during the week for which update
  // checks are disallowed.
  virtual Variable<WeeklyTimeIntervalVector>*
  var_disallowed_time_intervals() = 0;

  // Variable that determins whether we should powerwash and rollback on channel
  // downgrade for enrolled devices.
  virtual Variable<ChannelDowngradeBehavior>*
  var_channel_downgrade_behavior() = 0;

  // Variable that contains Chrome OS minimum required version. It contains a
  // Chrome OS version number.
  virtual Variable<base::Version>* var_device_minimum_version() = 0;

  // Variable that contains a token which maps to a Chrome OS Quick Fix Build to
  // which the device would be updated if not blocked by another policy.
  virtual Variable<std::string>* var_quick_fix_build_token() = 0;

  // Variable that contains the market segment defined in the device policy.
  virtual Variable<std::string>* var_market_segment() = 0;

  // Returns true if OOBE has been completed and if the device has been enrolled
  // as an enterprise device.
  virtual Variable<bool>* var_is_enterprise_enrolled() = 0;

 protected:
  DevicePolicyProvider() {}
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_DEVICE_POLICY_PROVIDER_H_
