// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "authpolicy/policy/device_policy_encoder.h"

#include <limits>
#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/callback.h>
#include <base/callback_helpers.h>
#include <base/check.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <components/policy/core/common/registry_dict.h>
#include <dbus/shill/dbus-constants.h>
#include <google/protobuf/repeated_field.h>

#include "bindings/chrome_device_policy.pb.h"
#include "bindings/policy_constants.h"

namespace em = enterprise_management;

namespace policy {
using ::google::protobuf::RepeatedPtrField;

// Types must be defined in order of definition in
// AutoUpdateSettingsProto_ConnectionType for the static_assert to work as
// expected.
constexpr std::pair<const char*, int> kConnectionTypes[] = {
    std::make_pair(
        shill::kTypeEthernet,
        em::AutoUpdateSettingsProto_ConnectionType_CONNECTION_TYPE_ETHERNET),
    std::make_pair(
        shill::kTypeWifi,
        em::AutoUpdateSettingsProto_ConnectionType_CONNECTION_TYPE_WIFI),
    std::make_pair(
        shill::kTypeCellular,
        em::AutoUpdateSettingsProto_ConnectionType_CONNECTION_TYPE_CELLULAR)};

constexpr size_t kConnectionTypesSize = base::size(kConnectionTypes);

// Integer range for DeviceLoginScreenScreenMagnifierType policy.
constexpr int kScreenMagnifierTypeRangeMin = 0;
constexpr int kScreenMagnifierTypeRangeMax = 2;

// Integer range for kDeviceCrostiniArcAdbSideloadingAllowed policy.
constexpr int kDeviceCrostiniArcAdbSideloadingAllowedRangeMin = 0;
constexpr int kDeviceCrostiniArcAdbSideloadingAllowedRangeMax = 2;

// Integer range for DeviceChromeVariations policy.
constexpr int kChromeVariationsRangeMin = 0;
constexpr int kChromeVariationsRangeMax = 2;

static_assert(em::AutoUpdateSettingsProto_ConnectionType_ConnectionType_MAX ==
                  kConnectionTypes[kConnectionTypesSize - 1].second,
              "Add all supported values here");

namespace {

// Translates string connection types to enums.
bool DecodeConnectionType(const std::string& value,
                          em::AutoUpdateSettingsProto_ConnectionType* type) {
  DCHECK(type);

  for (size_t n = 0; n < base::size(kConnectionTypes); ++n) {
    if (value.compare(kConnectionTypes[n].first) == 0) {
      int int_type = kConnectionTypes[n].second;
      DCHECK(em::AutoUpdateSettingsProto_ConnectionType_IsValid(int_type));
      *type = static_cast<em::AutoUpdateSettingsProto_ConnectionType>(int_type);
      return true;
    }
  }

  LOG(ERROR) << "Invalid connection type '" << value << "'.";
  return false;
}

// Parses the |json| string to a dictionary type base::Value. Returns nullopt on
// error and sets the |error| string.
base::Optional<base::Value> JsonToDictionary(const std::string& json,
                                             std::string* error) {
  DCHECK(error);
  auto root = base::JSONReader::ReadAndReturnValueWithError(
      json, base::JSON_ALLOW_TRAILING_COMMAS);
  if (!root.value) {
    *error = root.error_message;
    return base::nullopt;
  }

  if (!root.value->is_dict()) {
    *error = "JSON is not a dictionary: '" + json + "'";
    return base::nullopt;
  }
  return std::move(root.value);
}

#define CONVERT_DAY_OF_WEEK(day_of_week) \
  if (str == #day_of_week)               \
    return base::make_optional(em::WeeklyTimeProto::day_of_week);

base::Optional<em::WeeklyTimeProto::DayOfWeek> StringToDayOfWeek(
    const std::string& str) {
  CONVERT_DAY_OF_WEEK(MONDAY);
  CONVERT_DAY_OF_WEEK(TUESDAY);
  CONVERT_DAY_OF_WEEK(WEDNESDAY);
  CONVERT_DAY_OF_WEEK(THURSDAY);
  CONVERT_DAY_OF_WEEK(FRIDAY);
  CONVERT_DAY_OF_WEEK(SATURDAY);
  CONVERT_DAY_OF_WEEK(SUNDAY);
  return base::nullopt;
}

#undef CONVERT_WEEKDAY

// Converts a dictionary |value| to a WeeklyTimeProto |proto|.
bool EncodeWeeklyTimeProto(const base::Value& value,
                           em::WeeklyTimeProto* proto) {
  if (!value.is_dict())
    return false;

  const std::string* day_of_week_str = value.FindStringKey("day_of_week");
  if (!day_of_week_str)
    return false;

  auto day_of_week = StringToDayOfWeek(*day_of_week_str);
  auto time = value.FindIntKey("time");
  if (!day_of_week.has_value() || !time.has_value())
    return false;

  proto->set_day_of_week(*day_of_week);
  proto->set_time(*time);
  return true;
}

// Converts the dictionary |value| to a WeeklyTimeIntervalProto |proto|.
bool EncodeWeeklyTimeIntervalProto(const base::Value& value,
                                   em::WeeklyTimeIntervalProto* proto) {
  if (!value.is_dict())
    return false;

  const base::Value* start = value.FindDictKey("start");
  if (!start)
    return false;

  const base::Value* end = value.FindDictKey("end");
  if (!end)
    return false;

  DCHECK(start && end);
  return EncodeWeeklyTimeProto(*start, proto->mutable_start()) &&
         EncodeWeeklyTimeProto(*end, proto->mutable_end());
}

void CopyStringListPolicy(const std::vector<std::string>& list,
                          RepeatedPtrField<std::string>* proto_list) {
  *proto_list = {list.begin(), list.end()};
}

// Copies either `new_list` (preferred) or `old_list` to the specified
// proto_list. At least one of new_list or old_list must have a value.
void CopyStringListPolicyWithFallback(
    const base::Optional<std::vector<std::string>>& new_list,
    const base::Optional<std::vector<std::string>>& old_list,
    RepeatedPtrField<std::string>* proto_list) {
  if (new_list) {
    CopyStringListPolicy(new_list.value(), proto_list);
  } else {
    DCHECK(old_list);
    CopyStringListPolicy(old_list.value(), proto_list);
  }
}

}  // namespace

DevicePolicyEncoder::DevicePolicyEncoder(const RegistryDict* dict,
                                         const PolicyLevel level)
    : dict_(dict), level_(level) {}

void DevicePolicyEncoder::EncodePolicy(
    em::ChromeDeviceSettingsProto* policy) const {
  LOG_IF(INFO, log_policy_values_)
      << authpolicy::kColorPolicy << "Device policy ("
      << (level_ == POLICY_LEVEL_RECOMMENDED ? "recommended" : "mandatory")
      << ")" << authpolicy::kColorReset;
  if (level_ == POLICY_LEVEL_MANDATORY) {
    // All of the following policies support only mandatory level, so there's no
    // benefit on trying re-encoding them when the supported level is
    // recommended.
    EncodeLoginPolicies(policy);
    EncodeNetworkPolicies(policy);
    EncodeAutoUpdatePolicies(policy);
    EncodeAccessibilityPolicies(policy);
    EncodeGenericPolicies(policy);
  }
  EncodePoliciesWithPolicyOptions(policy);
}

void DevicePolicyEncoder::EncodeLoginPolicies(
    em::ChromeDeviceSettingsProto* policy) const {
  if (base::Optional<bool> value = EncodeBoolean(key::kDeviceGuestModeEnabled))
    policy->mutable_guest_mode_enabled()->set_guest_mode_enabled(value.value());
  if (base::Optional<bool> value = EncodeBoolean(key::kDeviceRebootOnShutdown))
    policy->mutable_reboot_on_shutdown()->set_reboot_on_shutdown(value.value());
  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceShowUserNamesOnSignin))
    policy->mutable_show_user_names()->set_show_user_names(value.value());
  if (base::Optional<bool> value = EncodeBoolean(key::kDeviceAllowNewUsers))
    policy->mutable_allow_new_users()->set_allow_new_users(value.value());
  // The original policy has been replaced by an inclusively named version. For
  // backwards compatibility, copy the original policy to the newly named proto
  // if no value exists for the newly named proto.
  base::Optional<std::vector<std::string>> user_allowlist_values =
      EncodeStringList(key::kDeviceUserAllowlist);
  base::Optional<std::vector<std::string>> user_whitelist_values =
      EncodeStringList(key::kDeviceUserWhitelist);
  if (user_allowlist_values || user_whitelist_values) {
    CopyStringListPolicyWithFallback(
        user_allowlist_values, user_whitelist_values,
        policy->mutable_user_allowlist()->mutable_user_allowlist());
  }
  if (user_whitelist_values) {
    CopyStringListPolicy(
        user_whitelist_values.value(),
        policy->mutable_user_whitelist()->mutable_user_whitelist());
  }
  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceEphemeralUsersEnabled))
    policy->mutable_ephemeral_users_enabled()->set_ephemeral_users_enabled(
        value.value());
  if (base::Optional<bool> value = EncodeBoolean(key::kDeviceAllowBluetooth))
    policy->mutable_allow_bluetooth()->set_allow_bluetooth(value.value());
  if (base::Optional<std::vector<std::string>> values =
          EncodeStringList(key::kDeviceLoginScreenExtensions)) {
    *policy->mutable_device_login_screen_extensions()
         ->mutable_device_login_screen_extensions() = {values.value().begin(),
                                                       values.value().end()};
  }
  if (base::Optional<std::string> value =
          EncodeString(key::kDeviceLoginScreenDomainAutoComplete)) {
    policy->mutable_login_screen_domain_auto_complete()
        ->set_login_screen_domain_auto_complete(value.value());
  }
  if (base::Optional<std::vector<std::string>> values =
          EncodeStringList(key::kDeviceLoginScreenLocales)) {
    *policy->mutable_login_screen_locales()->mutable_login_screen_locales() = {
        values.value().begin(), values.value().end()};
  }
  if (base::Optional<std::vector<std::string>> values =
          EncodeStringList(key::kDeviceLoginScreenInputMethods)) {
    *policy->mutable_login_screen_input_methods()
         ->mutable_login_screen_input_methods() = {values.value().begin(),
                                                   values.value().end()};
  }
  if (base::Optional<std::vector<std::string>> values = EncodeStringList(
          key::kDeviceLoginScreenAutoSelectCertificateForUrls)) {
    *policy->mutable_device_login_screen_auto_select_certificate_for_urls()
         ->mutable_login_screen_auto_select_certificate_rules() = {
        values.value().begin(), values.value().end()};
  }

  if (base::Optional<int> value =
          EncodeInteger(key::kDeviceRebootOnUserSignout)) {
    policy->mutable_device_reboot_on_user_signout()->set_reboot_on_signout_mode(
        static_cast<em::DeviceRebootOnUserSignoutProto_RebootOnSignoutMode>(
            value.value()));
  }

  if (base::Optional<bool> value =
          EncodeBoolean(key::kDevicePowerwashAllowed)) {
    policy->mutable_device_powerwash_allowed()->set_device_powerwash_allowed(
        value.value());
  }

  if (base::Optional<int> value = EncodeIntegerInRange(
          key::kDeviceChromeVariations, kChromeVariationsRangeMin,
          kChromeVariationsRangeMax)) {
    policy->mutable_device_chrome_variations_type()->set_value(value.value());
  }

  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceLoginScreenPrivacyScreenEnabled)) {
    policy->mutable_device_login_screen_privacy_screen_enabled()->set_enabled(
        value.value());
  }

  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceShowNumericKeyboardForPassword)) {
    policy->mutable_device_show_numeric_keyboard_for_password()->set_value(
        value.value());
  }

  if (base::Optional<std::vector<std::string>> values =
          EncodeStringList(key::kDeviceWebBasedAttestationAllowedUrls)) {
    *policy->mutable_device_web_based_attestation_allowed_urls()
         ->mutable_value()
         ->mutable_entries() = {values.value().begin(), values.value().end()};
  }

  if (base::Optional<std::string> value =
          EncodeString(key::kDeviceMinimumVersion))
    policy->mutable_device_minimum_version()->set_value(value.value());

  if (base::Optional<std::string> value =
          EncodeString(key::kDeviceMinimumVersionAueMessage)) {
    policy->mutable_device_minimum_version_aue_message()->set_value(
        value.value());
  }

  if (base::Optional<std::string> value =
          EncodeString(key::kRequiredClientCertificateForDevice)) {
    policy->mutable_required_client_certificate_for_device()
        ->set_required_client_certificate_for_device(value.value());
  }

  if (base::Optional<std::string> value =
          EncodeString(key::kSystemProxySettings)) {
    std::string error;
    base::Optional<base::Value> dict_value =
        JsonToDictionary(value.value(), &error);
    if (!dict_value) {
      LOG(ERROR) << "Failed to parse string as dictionary: '"
                 << (!error.empty() ? error : value.value()) << "' for policy '"
                 << key::kSystemProxySettings << "', ignoring.";
    } else {
      policy->mutable_system_proxy_settings()->set_system_proxy_settings(
          value.value());
    }
  }

  if (base::Optional<bool> value =
          EncodeBoolean(key::kManagedGuestSessionPrivacyWarningsEnabled)) {
    policy->mutable_managed_guest_session_privacy_warnings()->set_enabled(
        value.value());
  }
}

void DevicePolicyEncoder::EncodeNetworkPolicies(
    em::ChromeDeviceSettingsProto* policy) const {
  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceDataRoamingEnabled)) {
    policy->mutable_data_roaming_enabled()->set_data_roaming_enabled(
        value.value());
  }

  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceWiFiFastTransitionEnabled)) {
    policy->mutable_device_wifi_fast_transition_enabled()
        ->set_device_wifi_fast_transition_enabled(value.value());
  }

  if (base::Optional<std::string> value =
          EncodeString(key::kDeviceOpenNetworkConfiguration)) {
    policy->mutable_open_network_configuration()
        ->set_open_network_configuration(value.value());
  }

  if (base::Optional<std::string> value =
          EncodeString(key::kDeviceHostnameTemplate)) {
    policy->mutable_network_hostname()->set_device_hostname_template(
        value.value());
  }

  if (base::Optional<int> value =
          EncodeInteger(key::kDeviceKerberosEncryptionTypes)) {
    policy->mutable_device_kerberos_encryption_types()->set_types(
        static_cast<em::DeviceKerberosEncryptionTypesProto_Types>(
            value.value()));
  }

  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceDebugPacketCaptureAllowed)) {
    policy->mutable_device_debug_packet_capture_allowed()->set_allowed(
        value.value());
  }
}

void DevicePolicyEncoder::EncodeAutoUpdatePolicies(
    em::ChromeDeviceSettingsProto* policy) const {
  if (base::Optional<std::string> value =
          EncodeString(key::kChromeOsReleaseChannel))
    policy->mutable_release_channel()->set_release_channel(value.value());
  if (base::Optional<bool> value =
          EncodeBoolean(key::kChromeOsReleaseChannelDelegated)) {
    policy->mutable_release_channel()->set_release_channel_delegated(
        value.value());
  }
  if (base::Optional<std::string> value =
          EncodeString(key::kDeviceReleaseLtsTag)) {
    policy->mutable_release_channel()->set_release_lts_tag(value.value());
  }

  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceAutoUpdateDisabled))
    policy->mutable_auto_update_settings()->set_update_disabled(value.value());
  if (base::Optional<std::string> value =
          EncodeString(key::kDeviceTargetVersionPrefix)) {
    policy->mutable_auto_update_settings()->set_target_version_prefix(
        value.value());
  }
  if (base::Optional<int> value =
          EncodeInteger(key::kDeviceRollbackToTargetVersion)) {
    policy->mutable_auto_update_settings()->set_rollback_to_target_version(
        static_cast<em::AutoUpdateSettingsProto_RollbackToTargetVersion>(
            value.value()));
  }
  if (base::Optional<int> value =
          EncodeInteger(key::kDeviceRollbackAllowedMilestones)) {
    policy->mutable_auto_update_settings()->set_rollback_allowed_milestones(
        value.value());
  }

  // target_version_display_name is not actually a policy, but a display
  // string for target_version_prefix, so we ignore it. It seems to be
  // unreferenced as well.
  if (base::Optional<int> value =
          EncodeInteger(key::kDeviceUpdateScatterFactor)) {
    policy->mutable_auto_update_settings()->set_scatter_factor_in_seconds(
        value.value());
  }
  if (base::Optional<std::vector<std::string>> values =
          EncodeStringList(key::kDeviceUpdateAllowedConnectionTypes)) {
    auto list = policy->mutable_auto_update_settings();
    list->clear_allowed_connection_types();
    for (const std::string& value : values.value()) {
      em::AutoUpdateSettingsProto_ConnectionType type;
      if (DecodeConnectionType(value, &type))
        list->add_allowed_connection_types(type);
    }
  }
  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceUpdateHttpDownloadsEnabled)) {
    policy->mutable_auto_update_settings()->set_http_downloads_enabled(
        value.value());
  }
  if (base::Optional<bool> value = EncodeBoolean(key::kRebootAfterUpdate)) {
    policy->mutable_auto_update_settings()->set_reboot_after_update(
        value.value());
  }
  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceAutoUpdateP2PEnabled))
    policy->mutable_auto_update_settings()->set_p2p_enabled(value.value());
  if (base::Optional<std::string> value =
          EncodeString(key::kDeviceAutoUpdateTimeRestrictions)) {
    policy->mutable_auto_update_settings()->set_disallowed_time_intervals(
        value.value());
  }
  if (base::Optional<std::string> value =
          EncodeString(key::kDeviceUpdateStagingSchedule))
    policy->mutable_auto_update_settings()->set_staging_schedule(value.value());
  if (base::Optional<std::string> value =
          EncodeString(key::kDeviceQuickFixBuildToken)) {
    policy->mutable_auto_update_settings()->set_device_quick_fix_build_token(
        value.value());
  }
  if (base::Optional<std::string> value =
          EncodeString(key::kDeviceLoginScreenWebUsbAllowDevicesForUrls)) {
    policy->mutable_device_login_screen_webusb_allow_devices_for_urls()
        ->set_device_login_screen_webusb_allow_devices_for_urls(value.value());
  }
  if (base::Optional<int> value =
          EncodeInteger(key::kDeviceChannelDowngradeBehavior)) {
    if (em::AutoUpdateSettingsProto::ChannelDowngradeBehavior_IsValid(
            value.value())) {
      policy->mutable_auto_update_settings()->set_channel_downgrade_behavior(
          static_cast<em::AutoUpdateSettingsProto::ChannelDowngradeBehavior>(
              value.value()));
    } else {
      LOG(ERROR) << "Invalid enum value " << value.value() << " for policy "
                 << key::kDeviceChannelDowngradeBehavior;
    }
  }
}

void DevicePolicyEncoder::EncodeAccessibilityPolicies(
    em::ChromeDeviceSettingsProto* policy) const {
  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceLoginScreenDefaultLargeCursorEnabled)) {
    policy->mutable_accessibility_settings()
        ->set_login_screen_default_large_cursor_enabled(value.value());
  }
  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceLoginScreenDefaultSpokenFeedbackEnabled)) {
    policy->mutable_accessibility_settings()
        ->set_login_screen_default_spoken_feedback_enabled(value.value());
  }
  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceLoginScreenDefaultHighContrastEnabled)) {
    policy->mutable_accessibility_settings()
        ->set_login_screen_default_high_contrast_enabled(value.value());
  }
  if (base::Optional<int> value =
          EncodeInteger(key::kDeviceLoginScreenDefaultScreenMagnifierType)) {
    policy->mutable_accessibility_settings()
        ->set_login_screen_default_screen_magnifier_type(
            static_cast<em::AccessibilitySettingsProto_ScreenMagnifierType>(
                value.value()));
  }
  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceLoginScreenDefaultVirtualKeyboardEnabled)) {
    policy->mutable_accessibility_settings()
        ->set_login_screen_default_virtual_keyboard_enabled(value.value());
  }
}

void DevicePolicyEncoder::EncodePoliciesWithPolicyOptions(
    em::ChromeDeviceSettingsProto* policy) const {
  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceLoginScreenLargeCursorEnabled)) {
    em::AccessibilitySettingsProto* accessibility_settings =
        policy->mutable_accessibility_settings();
    accessibility_settings->set_login_screen_large_cursor_enabled(
        value.value());
    SetPolicyOptions(accessibility_settings
                         ->mutable_login_screen_large_cursor_enabled_options(),
                     level_);
  }

  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceLoginScreenAutoclickEnabled)) {
    em::AccessibilitySettingsProto* accessibility_settings =
        policy->mutable_accessibility_settings();
    accessibility_settings->set_login_screen_autoclick_enabled(value.value());
    SetPolicyOptions(accessibility_settings
                         ->mutable_login_screen_autoclick_enabled_options(),
                     level_);
  }

  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceLoginScreenCaretHighlightEnabled)) {
    em::AccessibilitySettingsProto* accessibility_settings =
        policy->mutable_accessibility_settings();
    accessibility_settings->set_login_screen_caret_highlight_enabled(
        value.value());
    SetPolicyOptions(
        accessibility_settings
            ->mutable_login_screen_caret_highlight_enabled_options(),
        level_);
  }

  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceLoginScreenCursorHighlightEnabled)) {
    em::AccessibilitySettingsProto* accessibility_settings =
        policy->mutable_accessibility_settings();
    accessibility_settings->set_login_screen_cursor_highlight_enabled(
        value.value());
    SetPolicyOptions(
        accessibility_settings
            ->mutable_login_screen_cursor_highlight_enabled_options(),
        level_);
  }

  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceLoginScreenDictationEnabled)) {
    em::AccessibilitySettingsProto* accessibility_settings =
        policy->mutable_accessibility_settings();
    accessibility_settings->set_login_screen_dictation_enabled(value.value());
    SetPolicyOptions(accessibility_settings
                         ->mutable_login_screen_dictation_enabled_options(),
                     level_);
  }

  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceLoginScreenHighContrastEnabled)) {
    em::AccessibilitySettingsProto* accessibility_settings =
        policy->mutable_accessibility_settings();
    accessibility_settings->set_login_screen_high_contrast_enabled(
        value.value());
    SetPolicyOptions(accessibility_settings
                         ->mutable_login_screen_high_contrast_enabled_options(),
                     level_);
  }

  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceLoginScreenMonoAudioEnabled)) {
    em::AccessibilitySettingsProto* accessibility_settings =
        policy->mutable_accessibility_settings();
    accessibility_settings->set_login_screen_mono_audio_enabled(value.value());
    SetPolicyOptions(accessibility_settings
                         ->mutable_login_screen_mono_audio_enabled_options(),
                     level_);
  }

  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceLoginScreenSelectToSpeakEnabled)) {
    em::AccessibilitySettingsProto* accessibility_settings =
        policy->mutable_accessibility_settings();
    accessibility_settings->set_login_screen_select_to_speak_enabled(
        value.value());
    SetPolicyOptions(
        accessibility_settings
            ->mutable_login_screen_select_to_speak_enabled_options(),
        level_);
  }

  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceLoginScreenSpokenFeedbackEnabled)) {
    em::AccessibilitySettingsProto* accessibility_settings =
        policy->mutable_accessibility_settings();
    accessibility_settings->set_login_screen_spoken_feedback_enabled(
        value.value());
    SetPolicyOptions(
        accessibility_settings
            ->mutable_login_screen_spoken_feedback_enabled_options(),
        level_);
  }

  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceLoginScreenStickyKeysEnabled)) {
    em::AccessibilitySettingsProto* accessibility_settings =
        policy->mutable_accessibility_settings();
    accessibility_settings->set_login_screen_sticky_keys_enabled(value.value());
    SetPolicyOptions(accessibility_settings
                         ->mutable_login_screen_sticky_keys_enabled_options(),
                     level_);
  }

  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceLoginScreenVirtualKeyboardEnabled)) {
    em::AccessibilitySettingsProto* accessibility_settings =
        policy->mutable_accessibility_settings();
    accessibility_settings->set_login_screen_virtual_keyboard_enabled(
        value.value());
    SetPolicyOptions(
        accessibility_settings
            ->mutable_login_screen_virtual_keyboard_enabled_options(),
        level_);
  }

  if (base::Optional<int> value = EncodeIntegerInRange(
          key::kDeviceLoginScreenScreenMagnifierType,
          kScreenMagnifierTypeRangeMin, kScreenMagnifierTypeRangeMax)) {
    em::AccessibilitySettingsProto* accessibility_settings =
        policy->mutable_accessibility_settings();
    accessibility_settings->set_login_screen_screen_magnifier_type(
        value.value());
    SetPolicyOptions(accessibility_settings
                         ->mutable_login_screen_screen_magnifier_type_options(),
                     level_);
  }
}

void DevicePolicyEncoder::EncodeGenericPolicies(
    em::ChromeDeviceSettingsProto* policy) const {
  if (base::Optional<int> value =
          EncodeInteger(key::kDevicePolicyRefreshRate)) {
    policy->mutable_device_policy_refresh_rate()
        ->set_device_policy_refresh_rate(value.value());
  }

  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceMetricsReportingEnabled))
    policy->mutable_metrics_enabled()->set_metrics_enabled(value.value());

  if (base::Optional<std::string> value = EncodeString(key::kSystemTimezone))
    policy->mutable_system_timezone()->set_timezone(value.value());
  if (base::Optional<int> value =
          EncodeInteger(key::kSystemTimezoneAutomaticDetection)) {
    policy->mutable_system_timezone()->set_timezone_detection_type(
        static_cast<em::SystemTimezoneProto_AutomaticTimezoneDetectionType>(
            value.value()));
  }
  if (base::Optional<bool> value = EncodeBoolean(key::kSystemUse24HourClock))
    policy->mutable_use_24hour_clock()->set_use_24hour_clock(value.value());

  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceAllowRedeemChromeOsRegistrationOffers)) {
    policy->mutable_allow_redeem_offers()->set_allow_redeem_offers(
        value.value());
  }

  if (base::Optional<std::string> value =
          EncodeString(key::kDeviceVariationsRestrictParameter))
    policy->mutable_variations_parameter()->set_parameter(value.value());

  if (base::Optional<std::string> value =
          EncodeString(key::kDeviceLoginScreenPowerManagement)) {
    policy->mutable_login_screen_power_management()
        ->set_login_screen_power_management(value.value());
  }

  if (base::Optional<int> value = EncodeInteger(key::kDisplayRotationDefault)) {
    policy->mutable_display_rotation_default()->set_display_rotation_default(
        static_cast<em::DisplayRotationDefaultProto_Rotation>(value.value()));
  }

  if (base::Optional<std::string> value =
          EncodeString(key::kDeviceDisplayResolution)) {
    policy->mutable_device_display_resolution()->set_device_display_resolution(
        value.value());
  }

  // The original policy has been replaced by an inclusively named version. For
  // backwards compatibility, copy the original policy to the newly named proto
  // if no value exists for the newly named proto.
  base::Optional<std::vector<std::string>> usb_detachable_allowlist_values =
      EncodeStringList(key::kUsbDetachableAllowlist);
  if (base::Optional<std::vector<std::string>> values =
          EncodeStringList(key::kUsbDetachableWhitelist)) {
    auto whitelist = policy->mutable_usb_detachable_whitelist();
    auto allowlist = policy->mutable_usb_detachable_allowlist();
    DCHECK(!whitelist->id_size());
    if (!usb_detachable_allowlist_values) {
      DCHECK(!allowlist->id_size());
    }
    for (const std::string& value : values.value()) {
      std::string error;
      base::Optional<base::Value> dict_value = JsonToDictionary(value, &error);
      if (!dict_value) {
        LOG(ERROR) << "Failed to parse string as dictionary: '"
                   << (!error.empty() ? error : value) << "' for policy '"
                   << key::kUsbDetachableWhitelist << "', ignoring.";
        continue;
      }

      base::Optional<int> vid = dict_value->FindIntKey("vendor_id");
      base::Optional<int> pid = dict_value->FindIntKey("product_id");
      if (!vid.has_value() || !pid.has_value()) {
        LOG(ERROR) << "Invalid JSON string '"
                   << (!error.empty() ? error : value) << "' for policy '"
                   << key::kUsbDetachableWhitelist << "', ignoring. Expected: "
                   << "'{\"vendor_id\"=<vid>, \"product_id\"=<pid>}'.";
        continue;
      }

      em::UsbDeviceIdProto* whitelist_entry = whitelist->add_id();
      whitelist_entry->set_vendor_id(*vid);
      whitelist_entry->set_product_id(*pid);

      if (!usb_detachable_allowlist_values) {
        em::UsbDeviceIdInclusiveProto* allowlist_entry = allowlist->add_id();
        allowlist_entry->set_vendor_id(*vid);
        allowlist_entry->set_product_id(*pid);
      }
    }
  }

  if (usb_detachable_allowlist_values) {
    auto list = policy->mutable_usb_detachable_allowlist();
    DCHECK(!list->id_size());
    for (const std::string& value : usb_detachable_allowlist_values.value()) {
      std::string error;
      base::Optional<base::Value> dict_value = JsonToDictionary(value, &error);
      if (!dict_value) {
        LOG(ERROR) << "Failed to parse string as dictionary: '"
                   << (!error.empty() ? error : value) << "' for policy '"
                   << key::kUsbDetachableAllowlist << "', ignoring.";
        continue;
      }
      base::Optional<int> vid = dict_value->FindIntKey("vendor_id");
      base::Optional<int> pid = dict_value->FindIntKey("product_id");
      if (!vid.has_value() || !pid.has_value()) {
        LOG(ERROR) << "Invalid JSON string '"
                   << (!error.empty() ? error : value) << "' for policy '"
                   << key::kUsbDetachableAllowlist << "', ignoring. Expected: "
                   << "'{\"vendor_id\": <vid>, \"product_id\": <pid>}'.";
        continue;
      }

      em::UsbDeviceIdInclusiveProto* entry = list->add_id();
      entry->set_vendor_id(*vid);
      entry->set_product_id(*pid);
    }
  }

  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceQuirksDownloadEnabled)) {
    policy->mutable_quirks_download_enabled()->set_quirks_download_enabled(
        value.value());
  }

  if (base::Optional<std::string> value =
          EncodeString(key::kDeviceWallpaperImage)) {
    policy->mutable_device_wallpaper_image()->set_device_wallpaper_image(
        value.value());
  }

  if (base::Optional<std::string> value = EncodeString(key::kDeviceOffHours)) {
    std::string error;
    base::Optional<base::Value> dict_value =
        JsonToDictionary(value.value(), &error);
    bool is_error = !dict_value;
    auto proto = std::make_unique<em::DeviceOffHoursProto>();
    if (!is_error) {
      const base::Value* intervals = dict_value->FindListKey("intervals");
      const base::Value* ignored_policy_proto_tags =
          dict_value->FindListKey("ignored_policy_proto_tags");
      const std::string* timezone = dict_value->FindStringKey("timezone");
      is_error = !intervals || !ignored_policy_proto_tags || !timezone;

      if (!is_error) {
        proto->set_timezone(*timezone);

        for (const base::Value& entry : intervals->GetList()) {
          is_error |=
              !EncodeWeeklyTimeIntervalProto(entry, proto->add_intervals());
        }

        for (const base::Value& entry : ignored_policy_proto_tags->GetList()) {
          int tag = 0;
          if (entry.is_int())
            tag = entry.GetInt();
          else
            is_error = true;
          proto->add_ignored_policy_proto_tags(tag);
        }
      }
    }

    if (is_error) {
      LOG(ERROR) << "Invalid JSON string '"
                 << (!error.empty() ? error : value.value()) << "' for policy '"
                 << key::kDeviceOffHours << "', ignoring. "
                 << "See policy_templates.json for example.";
    } else {
      policy->set_allocated_device_off_hours(proto.release());
    }
  }

  if (base::Optional<std::string> value = EncodeString(key::kCastReceiverName))
    policy->mutable_cast_receiver_name()->set_name(value.value());

  // The original policy has been replaced by an inclusively named version. For
  // backwards compatibility, copy the original policy to the newly named proto
  // if no value exists for the newly named proto.
  base::Optional<std::string> device_printers_value =
      EncodeString(key::kDevicePrinters);
  base::Optional<std::string> device_native_printers_value =
      EncodeString(key::kDeviceNativePrinters);
  if (device_printers_value || device_native_printers_value) {
    policy->mutable_device_printers()->set_external_policy(
        device_printers_value ? device_printers_value.value()
                              : device_native_printers_value.value());
  }
  if (device_native_printers_value) {
    policy->mutable_native_device_printers()->set_external_policy(
        device_native_printers_value.value());
  }

  // The original policy has been replaced by an inclusively named version. For
  // backwards compatibility, copy the original policy to the newly named proto
  // if no value exists for the newly named proto.
  base::Optional<int> device_printers_access_mode_value =
      EncodeInteger(key::kDevicePrintersAccessMode);
  base::Optional<int> device_native_printers_access_mode_value =
      EncodeInteger(key::kDeviceNativePrintersAccessMode);
  if (device_printers_access_mode_value ||
      device_native_printers_access_mode_value) {
    policy->mutable_device_printers_access_mode()->set_access_mode(
        static_cast<em::DevicePrintersAccessModeProto_AccessMode>(
            device_printers_access_mode_value
                ? device_printers_access_mode_value.value()
                : device_native_printers_access_mode_value.value()));
  }
  if (device_native_printers_access_mode_value) {
    policy->mutable_native_device_printers_access_mode()->set_access_mode(
        static_cast<em::DeviceNativePrintersAccessModeProto_AccessMode>(
            device_native_printers_access_mode_value.value()));
  }

  // The original policy has been replaced by an inclusively named version. For
  // backwards compatibility, copy the original policy to the newly named proto
  // if no value exists for the newly named proto.
  base::Optional<std::vector<std::string>> device_printers_blocklist_value =
      EncodeStringList(key::kDevicePrintersBlocklist);
  base::Optional<std::vector<std::string>>
      device_native_printers_blacklist_value =
          EncodeStringList(key::kDeviceNativePrintersBlacklist);
  if (device_printers_blocklist_value ||
      device_native_printers_blacklist_value) {
    CopyStringListPolicyWithFallback(
        device_printers_blocklist_value, device_native_printers_blacklist_value,
        policy->mutable_device_printers_blocklist()->mutable_blocklist());
  }
  if (device_native_printers_blacklist_value) {
    CopyStringListPolicy(device_native_printers_blacklist_value.value(),
                         policy->mutable_native_device_printers_blacklist()
                             ->mutable_blacklist());
  }

  // The original policy has been replaced by an inclusively named version. For
  // backwards compatibility, copy the original policy to the newly named proto
  // if no value exists for the newly named proto.
  base::Optional<std::vector<std::string>> device_printers_allowlist_value =
      EncodeStringList(key::kDevicePrintersAllowlist);
  base::Optional<std::vector<std::string>>
      device_native_printers_whitelist_value =
          EncodeStringList(key::kDeviceNativePrintersWhitelist);
  if (device_printers_allowlist_value ||
      device_native_printers_whitelist_value) {
    CopyStringListPolicyWithFallback(
        device_printers_allowlist_value, device_native_printers_whitelist_value,
        policy->mutable_device_printers_allowlist()->mutable_allowlist());
  }
  if (device_native_printers_whitelist_value) {
    CopyStringListPolicy(device_native_printers_whitelist_value.value(),
                         policy->mutable_native_device_printers_whitelist()
                             ->mutable_whitelist());
  }

  if (base::Optional<std::string> value =
          EncodeString(key::kDeviceExternalPrintServers)) {
    policy->mutable_external_print_servers()->set_external_policy(
        value.value());
  }
  if (base::Optional<std::vector<std::string>> values =
          EncodeStringList(key::kDeviceExternalPrintServersAllowlist)) {
    *policy->mutable_external_print_servers_allowlist()->mutable_allowlist() = {
        values.value().begin(), values.value().end()};
  }

  if (base::Optional<std::string> value =
          EncodeString(key::kTPMFirmwareUpdateSettings)) {
    std::string error;
    base::Optional<base::Value> dict_value =
        JsonToDictionary(value.value(), &error);
    if (!dict_value) {
      LOG(ERROR) << "Failed to parse string as dictionary: '"
                 << (!error.empty() ? error : value.value()) << "' for policy '"
                 << key::kTPMFirmwareUpdateSettings << "', ignoring.";
    } else {
      em::TPMFirmwareUpdateSettingsProto* settings =
          policy->mutable_tpm_firmware_update_settings();
      for (const auto& item : dict_value->DictItems()) {
        if (!item.second.is_bool()) {
          LOG(WARNING) << "Invalid value at: " << item.first;
          continue;
        }
        bool flag = item.second.GetBool();
        if (item.first == "allow-user-initiated-powerwash")
          settings->set_allow_user_initiated_powerwash(flag);
        else if (item.first == "allow-user-initiated-preserve-device-state")
          settings->set_allow_user_initiated_preserve_device_state(flag);
        else
          LOG(WARNING) << "Unknown JSON key: " << item.first;
      }
    }
  }

  if (base::Optional<bool> value =
          EncodeBoolean(key::kUnaffiliatedArcAllowed)) {
    policy->mutable_unaffiliated_arc_allowed()->set_unaffiliated_arc_allowed(
        value.value());
  }

  if (base::Optional<int> value =
          EncodeInteger(key::kDeviceUserPolicyLoopbackProcessingMode)) {
    policy->mutable_device_user_policy_loopback_processing_mode()->set_mode(
        static_cast<em::DeviceUserPolicyLoopbackProcessingModeProto::Mode>(
            value.value()));
  }

  if (base::Optional<bool> value =
          EncodeBoolean(key::kVirtualMachinesAllowed)) {
    policy->mutable_virtual_machines_allowed()->set_virtual_machines_allowed(
        value.value());
  }

  if (base::Optional<int> value =
          EncodeInteger(key::kDeviceMachinePasswordChangeRate)) {
    policy->mutable_device_machine_password_change_rate()->set_rate_days(
        value.value());
  }

  if (base::Optional<int> value = EncodeInteger(key::kDeviceGpoCacheLifetime)) {
    policy->mutable_device_gpo_cache_lifetime()->set_lifetime_hours(
        value.value());
  }

  if (base::Optional<int> value =
          EncodeInteger(key::kDeviceAuthDataCacheLifetime)) {
    policy->mutable_device_auth_data_cache_lifetime()->set_lifetime_hours(
        value.value());
  }

  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceUnaffiliatedCrostiniAllowed)) {
    policy->mutable_device_unaffiliated_crostini_allowed()
        ->set_device_unaffiliated_crostini_allowed(value.value());
  }

  if (base::Optional<bool> value = EncodeBoolean(key::kPluginVmAllowed))
    policy->mutable_plugin_vm_allowed()->set_plugin_vm_allowed(value.value());
  if (base::Optional<std::string> value =
          EncodeString(key::kPluginVmLicenseKey)) {
    policy->mutable_plugin_vm_license_key()->set_plugin_vm_license_key(
        value.value());
  }

  if (base::Optional<bool> value = EncodeBoolean(key::kDeviceWilcoDtcAllowed)) {
    policy->mutable_device_wilco_dtc_allowed()->set_device_wilco_dtc_allowed(
        value.value());
  }

  if (base::Optional<bool> value = EncodeBoolean(key::kDeviceBootOnAcEnabled))
    policy->mutable_device_boot_on_ac()->set_enabled(value.value());

  if (base::Optional<int> value =
          EncodeInteger(key::kDevicePowerPeakShiftBatteryThreshold)) {
    policy->mutable_device_power_peak_shift()->set_battery_threshold(
        value.value());
  }

  if (base::Optional<bool> value =
          EncodeBoolean(key::kDevicePowerPeakShiftEnabled))
    policy->mutable_device_power_peak_shift()->set_enabled(value.value());

  if (base::Optional<std::string> value =
          EncodeString(key::kDevicePowerPeakShiftDayConfig))
    policy->mutable_device_power_peak_shift()->set_day_configs(value.value());

  if (base::Optional<bool> value = EncodeBoolean(key::kDeviceWiFiAllowed)) {
    policy->mutable_device_wifi_allowed()->set_device_wifi_allowed(
        value.value());
  }

  if (base::Optional<std::string> value =
          EncodeString(key::kDeviceWilcoDtcConfiguration)) {
    policy->mutable_device_wilco_dtc_configuration()
        ->set_device_wilco_dtc_configuration(value.value());
  }

  if (base::Optional<int> value =
          EncodeInteger(key::kDeviceDockMacAddressSource)) {
    policy->mutable_device_dock_mac_address_source()->set_source(
        static_cast<em::DeviceDockMacAddressSourceProto::Source>(
            value.value()));
  }

  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceAdvancedBatteryChargeModeEnabled)) {
    policy->mutable_device_advanced_battery_charge_mode()->set_enabled(
        value.value());
  }

  if (base::Optional<std::string> value =
          EncodeString(key::kDeviceAdvancedBatteryChargeModeDayConfig)) {
    policy->mutable_device_advanced_battery_charge_mode()->set_day_configs(
        value.value());
  }

  if (base::Optional<int> value =
          EncodeInteger(key::kDeviceBatteryChargeMode)) {
    policy->mutable_device_battery_charge_mode()->set_battery_charge_mode(
        static_cast<em::DeviceBatteryChargeModeProto_BatteryChargeMode>(
            value.value()));
  }

  if (base::Optional<int> value =
          EncodeInteger(key::kDeviceBatteryChargeCustomStartCharging)) {
    policy->mutable_device_battery_charge_mode()->set_custom_charge_start(
        value.value());
  }

  if (base::Optional<int> value =
          EncodeInteger(key::kDeviceBatteryChargeCustomStopCharging)) {
    policy->mutable_device_battery_charge_mode()->set_custom_charge_stop(
        value.value());
  }

  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceUsbPowerShareEnabled))
    policy->mutable_device_usb_power_share()->set_enabled(value.value());

  if (base::Optional<int> value = EncodeIntegerInRange(
          key::kDeviceCrostiniArcAdbSideloadingAllowed,
          kDeviceCrostiniArcAdbSideloadingAllowedRangeMin,
          kDeviceCrostiniArcAdbSideloadingAllowedRangeMax)) {
    policy->mutable_device_crostini_arc_adb_sideloading_allowed()->set_mode(
        static_cast<
            em::DeviceCrostiniArcAdbSideloadingAllowedProto::AllowanceMode>(
            value.value()));
  }

  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceShowLowDiskSpaceNotification))
    policy->mutable_device_show_low_disk_space_notification()
        ->set_device_show_low_disk_space_notification(value.value());

  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceFamilyLinkAccountsAllowed)) {
    policy->mutable_family_link_accounts_allowed()
        ->set_family_link_accounts_allowed(value.value());
  }

  if (base::Optional<std::string> value =
          EncodeString(key::kDeviceArcDataSnapshotHours)) {
    std::string error;
    base::Optional<base::Value> dict_value =
        JsonToDictionary(value.value(), &error);
    if (!dict_value) {
      LOG(ERROR) << "Failed to parse string as dictionary: '"
                 << (!error.empty() ? error : value.value()) << "' for policy '"
                 << key::kDeviceArcDataSnapshotHours << "', ignoring.";
    } else {
      policy->mutable_arc_data_snapshot_hours()->set_arc_data_snapshot_hours(
          value.value());
    }
  }

  if (base::Optional<bool> value =
          EncodeBoolean(key::kDevicePciPeripheralDataAccessEnabled)) {
    policy->mutable_device_pci_peripheral_data_access_enabled_v2()->set_enabled(
        value.value());
  }

  if (base::Optional<bool> value = EncodeBoolean(key::kDeviceBorealisAllowed))
    policy->mutable_device_borealis_allowed()->set_allowed(value.value());

  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceSystemWideTracingEnabled)) {
    policy->mutable_device_system_wide_tracing_enabled()->set_enabled(
        value.value());
  }

  if (base::Optional<bool> value =
          EncodeBoolean(key::kDeviceAllowMGSToStoreDisplayProperties)) {
    policy->mutable_device_allow_mgs_to_store_display_properties()->set_value(
        value.value());
  }

  if (base::Optional<std::vector<std::string>> values =
          EncodeStringList(key::kDeviceAllowedBluetoothServices)) {
    *policy->mutable_device_allowed_bluetooth_services()
         ->mutable_allowlist() = {values.value().begin(), values.value().end()};
  }
}

base::Optional<bool> DevicePolicyEncoder::EncodeBoolean(
    const char* policy_name) const {
  return EncodeBooleanPolicy(policy_name, GetValueFromDictCallback(dict_),
                             log_policy_values_);
}

base::Optional<int> DevicePolicyEncoder::EncodeInteger(
    const char* policy_name) const {
  return EncodeIntegerInRange(policy_name, std::numeric_limits<int>::min(),
                              std::numeric_limits<int>::max());
}

base::Optional<int> DevicePolicyEncoder::EncodeIntegerInRange(
    const char* policy_name, int range_min, int range_max) const {
  return EncodeIntegerInRangePolicy(policy_name,
                                    GetValueFromDictCallback(dict_), range_min,
                                    range_max, log_policy_values_);
}

base::Optional<std::string> DevicePolicyEncoder::EncodeString(
    const char* policy_name) const {
  return EncodeStringPolicy(policy_name, GetValueFromDictCallback(dict_),
                            log_policy_values_);
}

base::Optional<std::vector<std::string>> DevicePolicyEncoder::EncodeStringList(
    const char* policy_name) const {
  const RegistryDict* key = dict_->GetKey(policy_name);
  if (!key)
    return base::nullopt;

  return EncodeStringListPolicy(policy_name, GetValueFromDictCallback(key),
                                log_policy_values_);
}

}  // namespace policy
