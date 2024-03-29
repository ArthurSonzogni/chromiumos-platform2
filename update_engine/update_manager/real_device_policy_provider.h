// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_REAL_DEVICE_POLICY_PROVIDER_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_REAL_DEVICE_POLICY_PROVIDER_H_

#include <memory>
#include <set>
#include <string>
#include <utility>

#include <brillo/message_loops/message_loop.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST
#include <oobe_config/metrics/enterprise_rollback_metrics_handler.h>
#include <policy/libpolicy.h>
#include <session_manager/dbus-proxies.h>

#include "update_engine/update_manager/device_policy_provider.h"
#include "update_engine/update_manager/generic_variables.h"

namespace chromeos_update_manager {

// |DevicePolicyProvider| concrete implementation.
class RealDevicePolicyProvider : public DevicePolicyProvider {
 public:
  RealDevicePolicyProvider(
      std::unique_ptr<org::chromium::SessionManagerInterfaceProxyInterface>
          session_manager_proxy,
      policy::PolicyProvider* policy_provider,
      std::unique_ptr<oobe_config::EnterpriseRollbackMetricsHandler>
          rollback_metrics)
      : policy_provider_(policy_provider),
        session_manager_proxy_(std::move(session_manager_proxy)),
        rollback_metrics_(std::move(rollback_metrics)) {}
  explicit RealDevicePolicyProvider(policy::PolicyProvider* policy_provider)
      : policy_provider_(policy_provider),
        rollback_metrics_(
            std::make_unique<oobe_config::EnterpriseRollbackMetricsHandler>()) {
  }
  RealDevicePolicyProvider(const RealDevicePolicyProvider&) = delete;
  RealDevicePolicyProvider& operator=(const RealDevicePolicyProvider&) = delete;

  ~RealDevicePolicyProvider();

  // Initializes the provider and returns whether it succeeded.
  bool Init();

  Variable<bool>* var_device_policy_is_loaded() override {
    return &var_device_policy_is_loaded_;
  }

  Variable<std::string>* var_release_channel() override {
    return &var_release_channel_;
  }

  Variable<bool>* var_release_channel_delegated() override {
    return &var_release_channel_delegated_;
  }

  Variable<std::string>* var_release_lts_tag() override {
    return &var_release_lts_tag_;
  }

  Variable<bool>* var_update_disabled() override {
    return &var_update_disabled_;
  }

  Variable<std::string>* var_target_version_prefix() override {
    return &var_target_version_prefix_;
  }

  Variable<RollbackToTargetVersion>* var_rollback_to_target_version() override {
    return &var_rollback_to_target_version_;
  }

  Variable<int>* var_rollback_allowed_milestones() override {
    return &var_rollback_allowed_milestones_;
  }

  Variable<base::TimeDelta>* var_scatter_factor() override {
    return &var_scatter_factor_;
  }

  Variable<std::set<chromeos_update_engine::ConnectionType>>*
  var_allowed_connection_types_for_update() override {
    return &var_allowed_connection_types_for_update_;
  }

  Variable<bool>* var_has_owner() override { return &var_has_owner_; }

  Variable<bool>* var_http_downloads_enabled() override {
    return &var_http_downloads_enabled_;
  }

  Variable<bool>* var_au_p2p_enabled() override { return &var_au_p2p_enabled_; }

  Variable<bool>* var_allow_kiosk_app_control_chrome_version() override {
    return &var_allow_kiosk_app_control_chrome_version_;
  }

  Variable<WeeklyTimeIntervalVector>* var_disallowed_time_intervals() override {
    return &var_disallowed_time_intervals_;
  }

  Variable<ChannelDowngradeBehavior>* var_channel_downgrade_behavior()
      override {
    return &var_channel_downgrade_behavior_;
  }

  Variable<base::Version>* var_device_minimum_version() override {
    return &var_device_minimum_version_;
  }

  Variable<std::string>* var_quick_fix_build_token() override {
    return &var_quick_fix_build_token_;
  }

  Variable<std::string>* var_market_segment() override {
    return &var_market_segment_;
  }

  Variable<bool>* var_is_enterprise_enrolled() override {
    return &var_is_enterprise_enrolled_;
  };

 private:
  FRIEND_TEST(UmRealDevicePolicyProviderTest, RefreshScheduledTest);
  FRIEND_TEST(UmRealDevicePolicyProviderTest, NonExistentDevicePolicyReloaded);
  FRIEND_TEST(UmRealDevicePolicyProviderTest, ValuesUpdated);
  FRIEND_TEST(UmRealDevicePolicyProviderTest, HasOwnerConverted);

  // A static handler for the |PropertyChangedCompleted| signal from the session
  // manager used as a callback.
  void OnPropertyChangedCompletedSignal(const std::string& success);

  // Called when the signal in |UpdateEngineLibcrosProxyResolvedInterface| is
  // connected.
  void OnSignalConnected(const std::string& interface_name,
                         const std::string& signal_name,
                         bool successful);

  // Schedules a call to periodically refresh the device policy.
  void RefreshDevicePolicyAndReschedule();

  // Reloads the device policy and updates all the exposed variables.
  void RefreshDevicePolicy();

  // Updates the async variable |var| based on the result value of the method
  // passed, which is a DevicePolicy getter method.
  template <typename T>
  void UpdateVariable(AsyncCopyVariable<T>* var,
                      bool (policy::DevicePolicy::*getter)(T*) const);

  // Updates the async variable |var| based on the result value of the getter
  // method passed, which is a wrapper getter on this class.
  template <typename T>
  void UpdateVariable(AsyncCopyVariable<T>* var,
                      bool (RealDevicePolicyProvider::*getter)(T*) const);

  // Wrapper for |DevicePolicy::GetRollbackToTargetVersion()| that converts the
  // result to |RollbackToTargetVersion|.
  bool ConvertRollbackToTargetVersion(
      RollbackToTargetVersion* rollback_to_target_version) const;

  // Wrapper for |DevicePolicy::GetScatterFactorInSeconds()| that converts the
  // result to a |base::TimeDelta|. It returns the same value as
  // |GetScatterFactorInSeconds()|.
  bool ConvertScatterFactor(base::TimeDelta* scatter_factor) const;

  // Wrapper for |DevicePolicy::GetAllowedConnectionTypesForUpdate()| that
  // converts the result to a set of |ConnectionType| elements instead of
  // strings.
  bool ConvertAllowedConnectionTypesForUpdate(
      std::set<chromeos_update_engine::ConnectionType>* allowed_types) const;

  // Wrapper for |DevicePolicy::GetUpdateTimeRestrictions()| that converts
  // the |DevicePolicy::WeeklyTimeInterval| structs to |WeeklyTimeInterval|
  // objects, which offer more functionality.
  bool ConvertDisallowedTimeIntervals(
      WeeklyTimeIntervalVector* disallowed_intervals_out) const;

  // Wrapper for |DevicePolicy::GetOwner()| that converts the result to a
  // boolean of whether the device has an owner. (Enterprise enrolled
  // devices do not have an owner).
  bool ConvertHasOwner(bool* has_owner) const;

  // Wrapper for |DevicePolicy::GetChannelDowngradeBehavior| that converts the
  // result to |ChannelDowngradeBehavior|.
  bool ConvertChannelDowngradeBehavior(
      ChannelDowngradeBehavior* channel_downgrade_behavior) const;

  // Wrapper for |DevicePolicy::GetDeviceMarketSegment| that converts the enum
  // values to a string to be sent to Omaha.
  bool ConvertDeviceMarketSegment(std::string* market_segment) const;

  // Used for fetching information about the device policy.
  policy::PolicyProvider* policy_provider_;

  // Used to schedule refreshes of the device policy.
  brillo::MessageLoop::TaskId scheduled_refresh_{
      brillo::MessageLoop::kTaskIdNull};

  // The DBus (mockable) session manager proxy.
  std::unique_ptr<org::chromium::SessionManagerInterfaceProxyInterface>
      session_manager_proxy_;

  std::unique_ptr<oobe_config::EnterpriseRollbackMetricsHandler>
      rollback_metrics_;

  // Variable exposing whether the policy is loaded.
  AsyncCopyVariable<bool> var_device_policy_is_loaded_{"policy_is_loaded",
                                                       false};

  // Variables mapping the exposed methods from the |policy::DevicePolicy|.
  AsyncCopyVariable<std::string> var_release_channel_{"release_channel"};
  AsyncCopyVariable<bool> var_release_channel_delegated_{
      "release_channel_delegated"};
  AsyncCopyVariable<std::string> var_release_lts_tag_{"release_lts_tag"};
  AsyncCopyVariable<bool> var_update_disabled_{"update_disabled"};
  AsyncCopyVariable<std::string> var_target_version_prefix_{
      "target_version_prefix"};
  AsyncCopyVariable<RollbackToTargetVersion> var_rollback_to_target_version_{
      "rollback_to_target_version"};
  AsyncCopyVariable<int> var_rollback_allowed_milestones_{
      "rollback_allowed_milestones"};
  AsyncCopyVariable<base::TimeDelta> var_scatter_factor_{"scatter_factor"};
  AsyncCopyVariable<std::set<chromeos_update_engine::ConnectionType>>
      var_allowed_connection_types_for_update_{
          "allowed_connection_types_for_update"};
  AsyncCopyVariable<bool> var_has_owner_{"owner"};
  AsyncCopyVariable<bool> var_http_downloads_enabled_{"http_downloads_enabled"};
  AsyncCopyVariable<bool> var_au_p2p_enabled_{"au_p2p_enabled"};
  AsyncCopyVariable<bool> var_allow_kiosk_app_control_chrome_version_{
      "allow_kiosk_app_control_chrome_version"};
  AsyncCopyVariable<WeeklyTimeIntervalVector> var_disallowed_time_intervals_{
      "update_time_restrictions"};
  AsyncCopyVariable<ChannelDowngradeBehavior> var_channel_downgrade_behavior_{
      "channel_downgrade_behavior"};
  AsyncCopyVariable<base::Version> var_device_minimum_version_{
      "device_minimum_version"};
  AsyncCopyVariable<std::string> var_quick_fix_build_token_{
      "quick_fix_build_token"};
  AsyncCopyVariable<std::string> var_market_segment_{"market_segment"};
  AsyncCopyVariable<bool> var_is_enterprise_enrolled_{"is_enterprise_enrolled"};
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_REAL_DEVICE_POLICY_PROVIDER_H_
