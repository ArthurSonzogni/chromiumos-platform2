// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unordered_set>

#include <base/strings/string_util.h>
#include <components/policy/core/common/registry_dict.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "authpolicy/policy/device_policy_encoder.h"
#include "authpolicy/policy/policy_encoder_test_base.h"
#include "bindings/chrome_device_policy.pb.h"
#include "bindings/policy_constants.h"

namespace em = enterprise_management;

namespace policy {

namespace {

// Converts a repeated string field to a vector.
std::vector<std::string> ToVector(
    const google::protobuf::RepeatedPtrField<std::string>& repeated_field) {
  return std::vector<std::string>(repeated_field.begin(), repeated_field.end());
}

// Converts a repeated int field to a vector.
std::vector<int> ToVector(
    const google::protobuf::RepeatedField<int>& repeated_field) {
  return std::vector<int>(repeated_field.begin(), repeated_field.end());
}

}  // namespace

// Checks whether all device policies are properly encoded from RegistryDict
// into em::ChromeDeviceSettingsProto. Makes sure no device policy is missing.
class DevicePolicyEncoderTest
    : public PolicyEncoderTestBase<em::ChromeDeviceSettingsProto> {
 public:
  DevicePolicyEncoderTest() {}
  ~DevicePolicyEncoderTest() override {}

 protected:
  void EncodeDict(em::ChromeDeviceSettingsProto* policy,
                  const RegistryDict* dict) override {
    DevicePolicyEncoder encoder(dict);
    *policy = em::ChromeDeviceSettingsProto();
    encoder.EncodePolicy(policy);
  }

  void MarkHandled(const char* key) override {
    handled_policy_keys_.insert(key);
  }

  // Returns a vector of all policy keys that were not encoded.
  std::vector<std::string> GetUnhandledPolicyKeys() const {
    std::vector<std::string> unhandled_policy_keys;
    for (const char** key = kDevicePolicyKeys; *key; ++key) {
      if (handled_policy_keys_.find(*key) == handled_policy_keys_.end())
        unhandled_policy_keys.push_back(*key);
    }
    return unhandled_policy_keys;
  }

 private:
  // Keeps track of handled device policies. Used to detect device policies that
  // device_policy_encoder forgets to encode.
  std::unordered_set<std::string> handled_policy_keys_;

  DISALLOW_COPY_AND_ASSIGN(DevicePolicyEncoderTest);
};

TEST_F(DevicePolicyEncoderTest, TestEncoding) {
  // Note that kStringList can't be constexpr, so we put them all here.
  const bool kBool = true;
  const int kInt = 123;
  const std::string kString = "val1";
  const std::vector<std::string> kStringList = {"val1", "val2", "val3"};

  em::ChromeDeviceSettingsProto policy;

  //
  // Login policies.
  //

  EncodeBoolean(&policy, key::kDeviceGuestModeEnabled, kBool);
  EXPECT_EQ(kBool, policy.guest_mode_enabled().guest_mode_enabled());

  EncodeBoolean(&policy, key::kDeviceRebootOnShutdown, kBool);
  EXPECT_EQ(kBool, policy.reboot_on_shutdown().reboot_on_shutdown());

  EncodeBoolean(&policy, key::kDeviceShowUserNamesOnSignin, kBool);
  EXPECT_EQ(kBool, policy.show_user_names().show_user_names());

  EncodeBoolean(&policy, key::kDeviceAllowNewUsers, kBool);
  EXPECT_EQ(kBool, policy.allow_new_users().allow_new_users());

  EncodeStringList(&policy, key::kDeviceUserWhitelist, kStringList);
  EXPECT_EQ(kStringList, ToVector(policy.user_whitelist().user_whitelist()));

  EncodeBoolean(&policy, key::kDeviceEphemeralUsersEnabled, kBool);
  EXPECT_EQ(kBool, policy.ephemeral_users_enabled().ephemeral_users_enabled());

  EncodeBoolean(&policy, key::kDeviceAllowBluetooth, kBool);
  EXPECT_EQ(kBool, policy.allow_bluetooth().allow_bluetooth());

  EncodeStringList(&policy, key::kDeviceLoginScreenExtensions, kStringList);
  EXPECT_EQ(kStringList, ToVector(policy.device_login_screen_extensions()
                                      .device_login_screen_extensions()));

  EncodeString(&policy, key::kDeviceLoginScreenDomainAutoComplete, kString);
  EXPECT_EQ(kString, policy.login_screen_domain_auto_complete()
                         .login_screen_domain_auto_complete());

  EncodeStringList(&policy, key::kDeviceLoginScreenLocales, kStringList);
  EXPECT_EQ(kStringList,
            ToVector(policy.login_screen_locales().login_screen_locales()));

  EncodeStringList(&policy, key::kDeviceLoginScreenInputMethods, kStringList);
  EXPECT_EQ(
      kStringList,
      ToVector(
          policy.login_screen_input_methods().login_screen_input_methods()));

  EncodeStringList(&policy, key::kDeviceLoginScreenAutoSelectCertificateForUrls,
                   kStringList);
  EXPECT_EQ(
      kStringList,
      ToVector(policy.device_login_screen_auto_select_certificate_for_urls()
                   .login_screen_auto_select_certificate_rules()));

  EncodeInteger(&policy, key::kDeviceRebootOnUserSignout,
                em::DeviceRebootOnUserSignoutProto_RebootOnSignoutMode_ALWAYS);
  EXPECT_EQ(em::DeviceRebootOnUserSignoutProto_RebootOnSignoutMode_ALWAYS,
            policy.device_reboot_on_user_signout().reboot_on_signout_mode());
  //
  // Network policies.
  //

  EncodeBoolean(&policy, key::kDeviceDataRoamingEnabled, kBool);
  EXPECT_EQ(kBool, policy.data_roaming_enabled().data_roaming_enabled());

  EncodeBoolean(&policy, key::kDeviceWiFiFastTransitionEnabled, kBool);
  EXPECT_EQ(kBool, policy.device_wifi_fast_transition_enabled()
                       .device_wifi_fast_transition_enabled());

  EncodeString(&policy, key::kDeviceOpenNetworkConfiguration, kString);
  EXPECT_EQ(kString,
            policy.open_network_configuration().open_network_configuration());

  EncodeString(&policy, key::kDeviceHostnameTemplate, kString);
  EXPECT_EQ(kString, policy.network_hostname().device_hostname_template());

  // The encoder of this policy converts ints to
  // DeviceKerberosEncryptionTypes::Types enums.
  EncodeInteger(&policy, key::kDeviceKerberosEncryptionTypes,
                em::DeviceKerberosEncryptionTypesProto::ENC_TYPES_ALL);
  EXPECT_EQ(em::DeviceKerberosEncryptionTypesProto::ENC_TYPES_ALL,
            policy.device_kerberos_encryption_types().types());

  //
  // Auto update policies.
  //

  EncodeString(&policy, key::kChromeOsReleaseChannel, kString);
  EXPECT_EQ(kString, policy.release_channel().release_channel());

  EncodeBoolean(&policy, key::kChromeOsReleaseChannelDelegated, kBool);
  EXPECT_EQ(kBool, policy.release_channel().release_channel_delegated());

  EncodeBoolean(&policy, key::kDeviceAutoUpdateDisabled, kBool);
  EXPECT_EQ(kBool, policy.auto_update_settings().update_disabled());

  EncodeString(&policy, key::kDeviceTargetVersionPrefix, kString);
  EXPECT_EQ(kString, policy.auto_update_settings().target_version_prefix());

  EncodeString(&policy, key::kDeviceQuickFixBuildToken, kString);
  EXPECT_EQ(kString,
            policy.auto_update_settings().device_quick_fix_build_token());

  // The encoder of this policy converts ints to RollbackToTargetVersion enums.
  EncodeInteger(&policy, key::kDeviceRollbackToTargetVersion,
                em::AutoUpdateSettingsProto::ROLLBACK_AND_POWERWASH);
  EXPECT_EQ(em::AutoUpdateSettingsProto::ROLLBACK_AND_POWERWASH,
            policy.auto_update_settings().rollback_to_target_version());

  EncodeInteger(&policy, key::kDeviceRollbackAllowedMilestones, kInt);
  EXPECT_EQ(kInt, policy.auto_update_settings().rollback_allowed_milestones());

  EncodeInteger(&policy, key::kDeviceUpdateScatterFactor, kInt);
  EXPECT_EQ(kInt, policy.auto_update_settings().scatter_factor_in_seconds());

  // The encoder of this policy converts connection type strings to enums.
  std::vector<std::string> str_types;
  std::vector<int> enum_types;
  for (size_t n = 0; n < kConnectionTypesSize; ++n) {
    str_types.push_back(kConnectionTypes[n].first);
    enum_types.push_back(kConnectionTypes[n].second);
  }
  EncodeStringList(&policy, key::kDeviceUpdateAllowedConnectionTypes,
                   str_types);
  EXPECT_EQ(enum_types,
            ToVector(policy.auto_update_settings().allowed_connection_types()));

  EncodeBoolean(&policy, key::kDeviceUpdateHttpDownloadsEnabled, kBool);
  EXPECT_EQ(kBool, policy.auto_update_settings().http_downloads_enabled());

  EncodeBoolean(&policy, key::kRebootAfterUpdate, kBool);
  EXPECT_EQ(kBool, policy.auto_update_settings().reboot_after_update());

  EncodeBoolean(&policy, key::kDeviceAutoUpdateP2PEnabled, kBool);
  EXPECT_EQ(kBool, policy.auto_update_settings().p2p_enabled());

  EncodeString(&policy, key::kDeviceAutoUpdateTimeRestrictions, kString);
  EXPECT_EQ(kString, policy.auto_update_settings().disallowed_time_intervals());

  EncodeString(&policy, key::kDeviceUpdateStagingSchedule, kString);
  EXPECT_EQ(kString, policy.auto_update_settings().staging_schedule());

  //
  // Accessibility policies.
  //

  EncodeBoolean(&policy, key::kDeviceLoginScreenDefaultLargeCursorEnabled,
                kBool);
  EXPECT_EQ(kBool, policy.accessibility_settings()
                       .login_screen_default_large_cursor_enabled());

  EncodeBoolean(&policy, key::kDeviceLoginScreenDefaultSpokenFeedbackEnabled,
                kBool);
  EXPECT_EQ(kBool, policy.accessibility_settings()
                       .login_screen_default_spoken_feedback_enabled());

  EncodeBoolean(&policy, key::kDeviceLoginScreenDefaultHighContrastEnabled,
                kBool);
  EXPECT_EQ(kBool, policy.accessibility_settings()
                       .login_screen_default_high_contrast_enabled());

  // The encoder of this policy converts ints to ScreenMagnifierType enums.
  EncodeInteger(&policy, key::kDeviceLoginScreenDefaultScreenMagnifierType,
                em::AccessibilitySettingsProto::SCREEN_MAGNIFIER_TYPE_FULL);
  EXPECT_EQ(em::AccessibilitySettingsProto::SCREEN_MAGNIFIER_TYPE_FULL,
            policy.accessibility_settings()
                .login_screen_default_screen_magnifier_type());

  EncodeBoolean(&policy, key::kDeviceLoginScreenDefaultVirtualKeyboardEnabled,
                kBool);
  EXPECT_EQ(kBool, policy.accessibility_settings()
                       .login_screen_default_virtual_keyboard_enabled());

  //
  // Generic policies.
  //

  EncodeInteger(&policy, key::kDevicePolicyRefreshRate, kInt);
  EXPECT_EQ(kInt,
            policy.device_policy_refresh_rate().device_policy_refresh_rate());

  EncodeBoolean(&policy, key::kDeviceMetricsReportingEnabled, kBool);
  EXPECT_EQ(kBool, policy.metrics_enabled().metrics_enabled());

  EncodeString(&policy, key::kSystemTimezone, kString);
  EXPECT_EQ(kString, policy.system_timezone().timezone());

  // The encoder of this policy converts ints to AutomaticTimezoneDetectionType
  // enums.
  EncodeInteger(&policy, key::kSystemTimezoneAutomaticDetection,
                em::SystemTimezoneProto::IP_ONLY);
  EXPECT_EQ(em::SystemTimezoneProto::IP_ONLY,
            policy.system_timezone().timezone_detection_type());

  EncodeBoolean(&policy, key::kSystemUse24HourClock, kBool);
  EXPECT_EQ(kBool, policy.use_24hour_clock().use_24hour_clock());

  EncodeBoolean(&policy, key::kDeviceAllowRedeemChromeOsRegistrationOffers,
                kBool);
  EXPECT_EQ(kBool, policy.allow_redeem_offers().allow_redeem_offers());

  EncodeString(&policy, key::kDeviceVariationsRestrictParameter, kString);
  EXPECT_EQ(kString, policy.variations_parameter().parameter());

  EncodeString(&policy, key::kDeviceLoginScreenPowerManagement, kString);
  EXPECT_EQ(
      kString,
      policy.login_screen_power_management().login_screen_power_management());

  // The encoder of this policy converts ints to Rotation enums.
  EncodeInteger(&policy, key::kDisplayRotationDefault,
                em::DisplayRotationDefaultProto::ROTATE_180);
  EXPECT_EQ(em::DisplayRotationDefaultProto::ROTATE_180,
            policy.display_rotation_default().display_rotation_default());

  EncodeString(&policy, key::kDeviceDisplayResolution, kString);
  EXPECT_EQ(kString,
            policy.device_display_resolution().device_display_resolution());

  // The encoder of this policy converts a JSON string to separate values.
  EncodeStringList(&policy, key::kUsbDetachableWhitelist,
                   {"{\"vendor_id\":123, \"product_id\":234}",
                    "{\"vendor_id\":345, \"product_id\":456}"});
  const auto& whitelist_proto = policy.usb_detachable_whitelist();
  EXPECT_EQ(123, whitelist_proto.id().Get(0).vendor_id());
  EXPECT_EQ(234, whitelist_proto.id().Get(0).product_id());
  EXPECT_EQ(345, whitelist_proto.id().Get(1).vendor_id());
  EXPECT_EQ(456, whitelist_proto.id().Get(1).product_id());

  EncodeBoolean(&policy, key::kDeviceQuirksDownloadEnabled, kBool);
  EXPECT_EQ(kBool, policy.quirks_download_enabled().quirks_download_enabled());

  EncodeString(&policy, key::kDeviceWallpaperImage, kString);
  EXPECT_EQ(kString, policy.device_wallpaper_image().device_wallpaper_image());

  EncodeString(&policy, key::kDeviceOffHours,
               R"!!!(
               {
                 "intervals":
                 [
                   {
                     "start": {
                       "day_of_week": "MONDAY",
                       "time": 12840000
                     },
                     "end": {
                       "day_of_week": "MONDAY",
                       "time": 21720000
                     }
                   },
                   {
                     "start": {
                       "day_of_week": "FRIDAY",
                       "time": 38640000
                     },
                     "end": {
                       "day_of_week": "FRIDAY",
                       "time": 57600000
                     }
                   }
                 ],
                 "timezone": "GMT",
                 "ignored_policy_proto_tags": [3, 8]
               })!!!");
  const auto& device_off_hours_proto = policy.device_off_hours();
  EXPECT_EQ(2, device_off_hours_proto.intervals_size());
  {
    const auto& interval1 = device_off_hours_proto.intervals().Get(0);
    const auto& interval2 = device_off_hours_proto.intervals().Get(1);
    EXPECT_EQ(em::WeeklyTimeProto::MONDAY, interval1.start().day_of_week());
    EXPECT_EQ(em::WeeklyTimeProto::MONDAY, interval1.end().day_of_week());
    EXPECT_EQ(12840000, interval1.start().time());
    EXPECT_EQ(21720000, interval1.end().time());
    EXPECT_EQ(em::WeeklyTimeProto::FRIDAY, interval2.start().day_of_week());
    EXPECT_EQ(em::WeeklyTimeProto::FRIDAY, interval2.end().day_of_week());
    EXPECT_EQ(38640000, interval2.start().time());
    EXPECT_EQ(57600000, interval2.end().time());
  }
  EXPECT_EQ("GMT", device_off_hours_proto.timezone());
  EXPECT_EQ(2, device_off_hours_proto.ignored_policy_proto_tags_size());
  EXPECT_EQ(3, device_off_hours_proto.ignored_policy_proto_tags().Get(0));
  EXPECT_EQ(8, device_off_hours_proto.ignored_policy_proto_tags().Get(1));

  EncodeString(&policy, key::kCastReceiverName, kString);
  EXPECT_EQ(kString, policy.cast_receiver_name().name());

  // The encoder of this policy converts ints to AccessMode enums.
  EncodeString(&policy, key::kDeviceNativePrinters, kString);
  EXPECT_EQ(kString, policy.native_device_printers().external_policy());

  EncodeInteger(&policy, key::kDeviceNativePrintersAccessMode,
                em::DeviceNativePrintersAccessModeProto::ACCESS_MODE_WHITELIST);
  EXPECT_EQ(em::DeviceNativePrintersAccessModeProto::ACCESS_MODE_WHITELIST,
            policy.native_device_printers_access_mode().access_mode());

  EncodeStringList(&policy, key::kDeviceNativePrintersWhitelist, kStringList);
  EXPECT_EQ(kStringList,
            ToVector(policy.native_device_printers_whitelist().whitelist()));

  EncodeStringList(&policy, key::kDeviceNativePrintersBlacklist, kStringList);
  EXPECT_EQ(kStringList,
            ToVector(policy.native_device_printers_blacklist().blacklist()));

  EncodeString(&policy, key::kTPMFirmwareUpdateSettings,
               "{\"allow-user-initiated-powerwash\":true,"
               " \"allow-user-initiated-preserve-device-state\":true}");
  EXPECT_EQ(
      true,
      policy.tpm_firmware_update_settings().allow_user_initiated_powerwash());
  EXPECT_EQ(true, policy.tpm_firmware_update_settings()
                      .allow_user_initiated_preserve_device_state());

  EncodeString(&policy, key::kMinimumRequiredChromeVersion, kString);
  EXPECT_EQ(kString, policy.minimum_required_version().chrome_version());

  EncodeBoolean(&policy, key::kUnaffiliatedArcAllowed, kBool);
  EXPECT_EQ(kBool,
            policy.unaffiliated_arc_allowed().unaffiliated_arc_allowed());

  EncodeBoolean(&policy, key::kPluginVmAllowed, kBool);
  EXPECT_EQ(kBool, policy.plugin_vm_allowed().plugin_vm_allowed());
  EncodeString(&policy, key::kPluginVmLicenseKey, kString);
  EXPECT_EQ(kString, policy.plugin_vm_license_key().plugin_vm_license_key());

  EncodeBoolean(&policy, key::kDeviceWilcoDtcAllowed, kBool);
  EXPECT_EQ(kBool,
            policy.device_wilco_dtc_allowed().device_wilco_dtc_allowed());

  EncodeBoolean(&policy, key::kDeviceBootOnAcEnabled, kBool);
  EXPECT_EQ(kBool, policy.device_boot_on_ac().enabled());

  EncodeInteger(&policy, key::kDevicePowerPeakShiftBatteryThreshold, kInt);
  EXPECT_EQ(kInt, policy.device_power_peak_shift().battery_threshold());
  EncodeBoolean(&policy, key::kDevicePowerPeakShiftEnabled, kBool);
  EXPECT_EQ(kBool, policy.device_power_peak_shift().enabled());
  EncodeString(&policy, key::kDevicePowerPeakShiftDayConfig, kString);
  EXPECT_EQ(kString, policy.device_power_peak_shift().day_configs());

  EncodeBoolean(&policy, key::kDeviceWiFiAllowed, kBool);
  EXPECT_EQ(kBool, policy.device_wifi_allowed().device_wifi_allowed());

  EncodeString(&policy, key::kDeviceWilcoDtcConfiguration, kString);
  EXPECT_EQ(
      kString,
      policy.device_wilco_dtc_configuration().device_wilco_dtc_configuration());

  // The encoder of this policy converts ints to
  // DeviceDockMacAddressSourceProto::Source enums.
  EncodeInteger(&policy, key::kDeviceDockMacAddressSource,
                em::DeviceDockMacAddressSourceProto::DOCK_NIC_MAC_ADDRESS);
  EXPECT_EQ(em::DeviceDockMacAddressSourceProto::DOCK_NIC_MAC_ADDRESS,
            policy.device_dock_mac_address_source().source());

  // The encoder of this policy converts ints to
  // DeviceUserPolicyLoopbackProcessingModeProto::Mode enums.
  EncodeInteger(
      &policy, key::kDeviceUserPolicyLoopbackProcessingMode,
      em::DeviceUserPolicyLoopbackProcessingModeProto::USER_POLICY_MODE_MERGE);
  EXPECT_EQ(
      em::DeviceUserPolicyLoopbackProcessingModeProto::USER_POLICY_MODE_MERGE,
      policy.device_user_policy_loopback_processing_mode().mode());

  EncodeString(&policy, key::kDeviceLoginScreenIsolateOrigins, kString);
  EXPECT_EQ(kString,
            policy.device_login_screen_isolate_origins().isolate_origins());

  EncodeBoolean(&policy, key::kDeviceLoginScreenSitePerProcess, kBool);
  EXPECT_EQ(kBool,
            policy.device_login_screen_site_per_process().site_per_process());

  EncodeBoolean(&policy, key::kVirtualMachinesAllowed, kBool);
  EXPECT_EQ(kBool,
            policy.virtual_machines_allowed().virtual_machines_allowed());

  EncodeInteger(&policy, key::kDeviceMachinePasswordChangeRate, kInt);
  EXPECT_EQ(kInt, policy.device_machine_password_change_rate().rate_days());

  EncodeInteger(&policy, key::kDeviceGpoCacheLifetime, kInt);
  EXPECT_EQ(kInt, policy.device_gpo_cache_lifetime().lifetime_hours());

  EncodeInteger(&policy, key::kDeviceAuthDataCacheLifetime, kInt);
  EXPECT_EQ(kInt, policy.device_auth_data_cache_lifetime().lifetime_hours());

  // The encoder of this policy converts ints to
  // SamlLoginAuthenticationTypeProto::Type enums.
  EncodeInteger(&policy, key::kDeviceSamlLoginAuthenticationType,
                em::SamlLoginAuthenticationTypeProto::TYPE_CLIENT_CERTIFICATE);
  EXPECT_EQ(
      em::SamlLoginAuthenticationTypeProto::TYPE_CLIENT_CERTIFICATE,
      policy.saml_login_authentication_type().saml_login_authentication_type());

  EncodeBoolean(&policy, key::kDeviceUnaffiliatedCrostiniAllowed, kBool);
  EXPECT_EQ(kBool, policy.device_unaffiliated_crostini_allowed()
                       .device_unaffiliated_crostini_allowed());

  //
  // Check whether all device policies have been handled.
  //

  std::vector<std::string> unhandled_policy_keys = GetUnhandledPolicyKeys();
  EXPECT_TRUE(unhandled_policy_keys.empty())
      << "Unhandled policy detected.\n"
      << "Please handle the following policies in "
      << "device_policy_encoder.cc and device_policy_encoder_unittest.cc:\n"
      << "  " << base::JoinString(unhandled_policy_keys, "\n  ");
}

}  // namespace policy
