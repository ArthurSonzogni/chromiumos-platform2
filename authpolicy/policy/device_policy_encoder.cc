// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "authpolicy/policy/device_policy_encoder.h"

#include <memory>
#include <utility>

#include <base/json/json_reader.h>
#include <base/strings/string_number_conversions.h>
#include <components/policy/core/common/registry_dict.h>
#include <dbus/shill/dbus-constants.h>

#include "authpolicy/log_colors.h"
#include "authpolicy/policy/policy_encoder_helper.h"
#include "bindings/chrome_device_policy.pb.h"
#include "bindings/policy_constants.h"

namespace em = enterprise_management;

namespace {

const char* kColorPolicy = authpolicy::kColorPolicy;
const char* kColorReset = authpolicy::kColorReset;

}  // namespace

namespace policy {

const std::pair<const char*, int> kConnectionTypes[] = {
    std::make_pair(
        shill::kTypeEthernet,
        em::AutoUpdateSettingsProto_ConnectionType_CONNECTION_TYPE_ETHERNET),
    std::make_pair(
        shill::kTypeWifi,
        em::AutoUpdateSettingsProto_ConnectionType_CONNECTION_TYPE_WIFI),
    std::make_pair(
        shill::kTypeWimax,
        em::AutoUpdateSettingsProto_ConnectionType_CONNECTION_TYPE_WIMAX),
    std::make_pair(
        shill::kTypeBluetooth,
        em::AutoUpdateSettingsProto_ConnectionType_CONNECTION_TYPE_BLUETOOTH),
    std::make_pair(
        shill::kTypeCellular,
        em::AutoUpdateSettingsProto_ConnectionType_CONNECTION_TYPE_CELLULAR)};

constexpr size_t kConnectionTypesSize = arraysize(kConnectionTypes);

static_assert(
    em::AutoUpdateSettingsProto_ConnectionType_ConnectionType_ARRAYSIZE ==
        static_cast<int>(kConnectionTypesSize),
    "Add all values here");

namespace {

// Translates string connection types to enums.
bool DecodeConnectionType(const std::string& value,
                          em::AutoUpdateSettingsProto_ConnectionType* type) {
  DCHECK(type);

  for (size_t n = 0; n < arraysize(kConnectionTypes); ++n) {
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

// Parses the |json| string to a base::DictionaryValue. Returns nullptr on error
// and sets the |error| string.
std::unique_ptr<base::DictionaryValue> JsonToDictionary(const std::string& json,
                                                        std::string* error) {
  DCHECK(error);
  std::unique_ptr<base::Value> root = base::JSONReader::ReadAndReturnError(
      json, base::JSON_ALLOW_TRAILING_COMMAS, NULL, error);
  if (!root)
    return nullptr;  // |error| is set by ReadAndReturnError().

  std::unique_ptr<base::DictionaryValue> dict_value =
      base::DictionaryValue::From(std::move(root));
  if (!dict_value)
    *error = "JSON is not a dictionary: '" + json + "'";
  return dict_value;
}

#define CONVERT_DAY_OF_WEEK(day_of_week)    \
  if (str == #day_of_week) {                \
    *wd = em::WeeklyTimeProto::day_of_week; \
    return true;                            \
  }

bool StringToDayOfWeek(const std::string& str,
                       em::WeeklyTimeProto::DayOfWeek* wd) {
  CONVERT_DAY_OF_WEEK(MONDAY);
  CONVERT_DAY_OF_WEEK(TUESDAY);
  CONVERT_DAY_OF_WEEK(WEDNESDAY);
  CONVERT_DAY_OF_WEEK(THURSDAY);
  CONVERT_DAY_OF_WEEK(FRIDAY);
  CONVERT_DAY_OF_WEEK(SATURDAY);
  CONVERT_DAY_OF_WEEK(SUNDAY);
  return false;
}

#undef CONVERT_WEEKDAY

// Converts a dictionary |value| to a WeeklyTimeProto |proto|.
bool EncodeWeeklyTimeProto(const base::DictionaryValue& value,
                           em::WeeklyTimeProto* proto) {
  std::string day_of_week_str;
  em::WeeklyTimeProto::DayOfWeek day_of_week = em::WeeklyTimeProto::MONDAY;
  int time = 0;
  if (!value.GetString("day_of_week", &day_of_week_str) ||
      !StringToDayOfWeek(day_of_week_str, &day_of_week) ||
      !value.GetInteger("time", &time)) {
    return false;
  }

  proto->set_day_of_week(day_of_week);
  proto->set_time(time);
  return true;
}

// Converts the dictionary |value| to a DeviceOffHoursIntervalProto |proto|.
bool EncodeDeviceOffHoursIntervalProto(const base::Value& value,
                                       em::DeviceOffHoursIntervalProto* proto) {
  const base::DictionaryValue* dict = nullptr;
  if (!value.GetAsDictionary(&dict))
    return false;

  const base::DictionaryValue* start = nullptr;
  if (!dict->GetDictionary("start", &start))
    return false;

  const base::DictionaryValue* end = nullptr;
  if (!dict->GetDictionary("end", &end))
    return false;

  DCHECK(start && end);
  return EncodeWeeklyTimeProto(*start, proto->mutable_start()) &&
         EncodeWeeklyTimeProto(*end, proto->mutable_end());
}

}  // namespace

void DevicePolicyEncoder::EncodePolicy(
    em::ChromeDeviceSettingsProto* policy) const {
  LOG_IF(INFO, log_policy_values_)
      << kColorPolicy << "Device policy" << kColorReset;
  EncodeLoginPolicies(policy);
  EncodeNetworkPolicies(policy);
  EncodeAutoUpdatePolicies(policy);
  EncodeAccessibilityPolicies(policy);
  EncodeGenericPolicies(policy);
}

void DevicePolicyEncoder::EncodeLoginPolicies(
    em::ChromeDeviceSettingsProto* policy) const {
  EncodeBoolean(key::kDeviceGuestModeEnabled, [policy](bool value) {
    policy->mutable_guest_mode_enabled()->set_guest_mode_enabled(value);
  });
  EncodeBoolean(key::kDeviceRebootOnShutdown, [policy](bool value) {
    policy->mutable_reboot_on_shutdown()->set_reboot_on_shutdown(value);
  });
  EncodeBoolean(key::kDeviceShowUserNamesOnSignin, [policy](bool value) {
    policy->mutable_show_user_names()->set_show_user_names(value);
  });
  EncodeBoolean(key::kDeviceAllowNewUsers, [policy](bool value) {
    policy->mutable_allow_new_users()->set_allow_new_users(value);
  });
  EncodeStringList(key::kDeviceUserWhitelist,
                   [policy](const std::vector<std::string>& values) {
                     auto list = policy->mutable_user_whitelist();
                     list->clear_user_whitelist();
                     for (const std::string& value : values)
                       list->add_user_whitelist(value);
                   });
  EncodeBoolean(key::kDeviceEphemeralUsersEnabled, [policy](bool value) {
    policy->mutable_ephemeral_users_enabled()->set_ephemeral_users_enabled(
        value);
  });

  EncodeBoolean(key::kDeviceAllowBluetooth, [policy](bool value) {
    policy->mutable_allow_bluetooth()->set_allow_bluetooth(value);
  });
  EncodeStringList(key::kDeviceLoginScreenAppInstallList,
                   [policy](const std::vector<std::string>& values) {
                     auto list =
                         policy->mutable_device_login_screen_app_install_list();
                     list->clear_device_login_screen_app_install_list();
                     for (const std::string& value : values)
                       list->add_device_login_screen_app_install_list(value);
                   });
  EncodeStringList(key::kDeviceLoginScreenLocales,
                   [policy](const std::vector<std::string>& values) {
                     auto list = policy->mutable_login_screen_locales();
                     list->clear_login_screen_locales();
                     for (const std::string& value : values)
                       list->add_login_screen_locales(value);
                   });
  EncodeStringList(key::kDeviceLoginScreenInputMethods,
                   [policy](const std::vector<std::string>& values) {
                     auto list = policy->mutable_login_screen_input_methods();
                     list->clear_login_screen_input_methods();
                     for (const std::string& value : values)
                       list->add_login_screen_input_methods(value);
                   });
  EncodeStringList(
      key::kDeviceLoginScreenAutoSelectCertificateForUrls,
      [policy](const std::vector<std::string>& values) {
        // Abbreviate |policy| to |p| to prevent issues with 80
        // character line length limit.
        em::ChromeDeviceSettingsProto* p = policy;
        auto* list =
            p->mutable_device_login_screen_auto_select_certificate_for_urls();
        list->clear_login_screen_auto_select_certificate_rules();
        for (const std::string& value : values)
          list->add_login_screen_auto_select_certificate_rules(value);
      });
}

void DevicePolicyEncoder::EncodeNetworkPolicies(
    em::ChromeDeviceSettingsProto* policy) const {
  EncodeBoolean(key::kDeviceDataRoamingEnabled, [policy](bool value) {
    policy->mutable_data_roaming_enabled()->set_data_roaming_enabled(value);
  });

  EncodeString(key::kDeviceOpenNetworkConfiguration,
               [policy](const std::string& value) {
                 policy->mutable_open_network_configuration()
                     ->set_open_network_configuration(value);
               });

  EncodeString(
      key::kDeviceHostnameTemplate, [policy](const std::string& value) {
        policy->mutable_network_hostname()->set_device_hostname_template(value);
      });

  EncodeInteger(key::kDeviceKerberosEncryptionTypes, [policy](int value) {
    policy->mutable_device_kerberos_encryption_types()->set_types(
        static_cast<em::DeviceKerberosEncryptionTypesProto_Types>(value));
  });
}

void DevicePolicyEncoder::EncodeAutoUpdatePolicies(
    em::ChromeDeviceSettingsProto* policy) const {
  EncodeString(key::kChromeOsReleaseChannel,
               [policy](const std::string& value) {
                 policy->mutable_release_channel()->set_release_channel(value);
               });
  EncodeBoolean(key::kChromeOsReleaseChannelDelegated, [policy](bool value) {
    policy->mutable_release_channel()->set_release_channel_delegated(value);
  });

  EncodeBoolean(key::kDeviceAutoUpdateDisabled, [policy](bool value) {
    policy->mutable_auto_update_settings()->set_update_disabled(value);
  });
  EncodeString(key::kDeviceTargetVersionPrefix, [policy](
                                                    const std::string& value) {
    policy->mutable_auto_update_settings()->set_target_version_prefix(value);
  });
  // target_version_display_name is not actually a policy, but a display
  // string for target_version_prefix, so we ignore it. It seems to be
  // unreferenced as well.
  EncodeInteger(key::kDeviceUpdateScatterFactor, [policy](int value) {
    policy->mutable_auto_update_settings()->set_scatter_factor_in_seconds(
        value);
  });
  EncodeStringList(key::kDeviceUpdateAllowedConnectionTypes,
                   [policy](const std::vector<std::string>& values) {
                     auto list = policy->mutable_auto_update_settings();
                     list->clear_allowed_connection_types();
                     for (const std::string& value : values) {
                       em::AutoUpdateSettingsProto_ConnectionType type;
                       if (DecodeConnectionType(value, &type))
                         list->add_allowed_connection_types(type);
                     }
                   });
  EncodeBoolean(key::kDeviceUpdateHttpDownloadsEnabled, [policy](bool value) {
    policy->mutable_auto_update_settings()->set_http_downloads_enabled(value);
  });
  EncodeBoolean(key::kRebootAfterUpdate, [policy](bool value) {
    policy->mutable_auto_update_settings()->set_reboot_after_update(value);
  });
  EncodeBoolean(key::kDeviceAutoUpdateP2PEnabled, [policy](bool value) {
    policy->mutable_auto_update_settings()->set_p2p_enabled(value);
  });
}

void DevicePolicyEncoder::EncodeAccessibilityPolicies(
    em::ChromeDeviceSettingsProto* policy) const {
  EncodeBoolean(key::kDeviceLoginScreenDefaultLargeCursorEnabled,
                [policy](bool value) {
                  policy->mutable_accessibility_settings()
                      ->set_login_screen_default_large_cursor_enabled(value);
                });
  EncodeBoolean(key::kDeviceLoginScreenDefaultSpokenFeedbackEnabled,
                [policy](bool value) {
                  policy->mutable_accessibility_settings()
                      ->set_login_screen_default_spoken_feedback_enabled(value);
                });
  EncodeBoolean(key::kDeviceLoginScreenDefaultHighContrastEnabled,
                [policy](bool value) {
                  policy->mutable_accessibility_settings()
                      ->set_login_screen_default_high_contrast_enabled(value);
                });
  EncodeInteger(
      key::kDeviceLoginScreenDefaultScreenMagnifierType, [policy](int value) {
        policy->mutable_accessibility_settings()
            ->set_login_screen_default_screen_magnifier_type(
                static_cast<em::AccessibilitySettingsProto_ScreenMagnifierType>(
                    value));
      });
  EncodeBoolean(
      key::kDeviceLoginScreenDefaultVirtualKeyboardEnabled,
      [policy](bool value) {
        policy->mutable_accessibility_settings()
            ->set_login_screen_default_virtual_keyboard_enabled(value);
      });
}

void DevicePolicyEncoder::EncodeGenericPolicies(
    em::ChromeDeviceSettingsProto* policy) const {
  EncodeInteger(key::kDevicePolicyRefreshRate, [policy](int value) {
    policy->mutable_device_policy_refresh_rate()
        ->set_device_policy_refresh_rate(value);
  });

  EncodeBoolean(key::kDeviceMetricsReportingEnabled, [policy](bool value) {
    policy->mutable_metrics_enabled()->set_metrics_enabled(value);
  });

  EncodeString(key::kSystemTimezone, [policy](const std::string& value) {
    policy->mutable_system_timezone()->set_timezone(value);
  });
  EncodeInteger(key::kSystemTimezoneAutomaticDetection, [policy](int value) {
    policy->mutable_system_timezone()->set_timezone_detection_type(
        static_cast<em::SystemTimezoneProto_AutomaticTimezoneDetectionType>(
            value));
  });
  EncodeBoolean(key::kSystemUse24HourClock, [policy](bool value) {
    policy->mutable_use_24hour_clock()->set_use_24hour_clock(value);
  });

  EncodeBoolean(
      key::kDeviceAllowRedeemChromeOsRegistrationOffers, [policy](bool value) {
        policy->mutable_allow_redeem_offers()->set_allow_redeem_offers(value);
      });

  EncodeStringList(key::kDeviceStartUpFlags,
                   [policy](const std::vector<std::string>& values) {
                     auto list = policy->mutable_start_up_flags();
                     list->clear_flags();
                     for (const std::string& value : values)
                       list->add_flags(value);
                   });

  EncodeString(key::kDeviceVariationsRestrictParameter,
               [policy](const std::string& value) {
                 policy->mutable_variations_parameter()->set_parameter(value);
               });

  EncodeString(key::kDeviceLoginScreenPowerManagement,
               [policy](const std::string& value) {
                 policy->mutable_login_screen_power_management()
                     ->set_login_screen_power_management(value);
               });

  EncodeBoolean(key::kDeviceBlockDevmode, [policy](bool value) {
    policy->mutable_system_settings()->set_block_devmode(value);
  });

  EncodeInteger(key::kDisplayRotationDefault, [policy](int value) {
    policy->mutable_display_rotation_default()->set_display_rotation_default(
        static_cast<em::DisplayRotationDefaultProto_Rotation>(value));
  });

  EncodeStringList(
      key::kUsbDetachableWhitelist,
      [policy](const std::vector<std::string>& values) {
        auto list = policy->mutable_usb_detachable_whitelist();
        list->clear_id();
        for (const std::string& value : values) {
          std::string error;
          std::unique_ptr<base::DictionaryValue> dict_value =
              JsonToDictionary(value, &error);
          int vid, pid;
          if (!dict_value || !dict_value->GetInteger("vendor_id", &vid) ||
              !dict_value->GetInteger("product_id", &pid)) {
            LOG(ERROR) << "Invalid JSON string '"
                       << (!error.empty() ? error : value) << "' for policy '"
                       << key::kUsbDetachableWhitelist
                       << "', ignoring. Expected: "
                       << "'{\"vendor_id\"=<vid>, \"product_id\"=<pid>}'.";
            continue;
          }

          em::UsbDeviceIdProto* entry = list->add_id();
          entry->set_vendor_id(vid);
          entry->set_product_id(pid);
        }
      });

  EncodeBoolean(key::kDeviceQuirksDownloadEnabled, [policy](bool value) {
    policy->mutable_quirks_download_enabled()->set_quirks_download_enabled(
        value);
  });

  EncodeString(key::kDeviceWallpaperImage, [policy](const std::string& value) {
    policy->mutable_device_wallpaper_image()->set_device_wallpaper_image(value);
  });

  EncodeString(key::kDeviceOffHours, [policy](const std::string& value) {
    std::string error;
    std::unique_ptr<base::DictionaryValue> dict_value =
        JsonToDictionary(value, &error);
    const base::ListValue* intervals = nullptr;
    const base::ListValue* ignored_policy_proto_tags = nullptr;
    std::string timezone;
    bool is_error = !dict_value ||
                    !dict_value->GetList("intervals", &intervals) ||
                    !dict_value->GetList("ignored_policy_proto_tags",
                                         &ignored_policy_proto_tags) ||
                    !dict_value->GetString("timezone", &timezone);
    auto proto = std::make_unique<em::DeviceOffHoursProto>();
    if (!is_error) {
      proto->set_timezone(timezone);

      for (const base::Value* entry : *intervals) {
        is_error |=
            !EncodeDeviceOffHoursIntervalProto(*entry, proto->add_intervals());
      }

      for (const base::Value* entry : *ignored_policy_proto_tags) {
        int tag = 0;
        is_error |= !entry->GetAsInteger(&tag);
        proto->add_ignored_policy_proto_tags(tag);
      }
    }

    if (is_error) {
      LOG(ERROR) << "Invalid JSON string '" << (!error.empty() ? error : value)
                 << "' for policy '" << key::kDeviceOffHours << "', ignoring. "
                 << "See policy_templates.json for example.";
      return;
    }

    policy->set_allocated_device_off_hours(proto.release());
  });

  EncodeString(key::kCastReceiverName, [policy](const std::string& value) {
    policy->mutable_cast_receiver_name()->set_name(value);
  });

  EncodeString(key::kDeviceNativePrinters, [policy](const std::string& value) {
    policy->mutable_native_device_printers()->set_external_policy(value);
  });
  EncodeInteger(key::kDeviceNativePrintersAccessMode, [policy](int value) {
    policy->mutable_native_device_printers_access_mode()->set_access_mode(
        static_cast<em::DeviceNativePrintersAccessModeProto_AccessMode>(value));
  });
  EncodeStringList(key::kDeviceNativePrintersBlacklist,
                   [policy](const std::vector<std::string>& values) {
                     auto list =
                         policy->mutable_native_device_printers_blacklist();
                     list->clear_blacklist();
                     for (const std::string& value : values)
                       list->add_blacklist(value);
                   });
  EncodeStringList(key::kDeviceNativePrintersWhitelist,
                   [policy](const std::vector<std::string>& values) {
                     auto list =
                         policy->mutable_native_device_printers_whitelist();
                     list->clear_whitelist();
                     for (const std::string& value : values)
                       list->add_whitelist(value);
                   });

  EncodeString(
      key::kTPMFirmwareUpdateSettings, [policy](const std::string& value) {
        std::string error;
        std::unique_ptr<base::DictionaryValue> dict_value =
            JsonToDictionary(value, &error);
        bool allow_user_initiated_powerwash;
        if (!dict_value ||
            !dict_value->GetBoolean("allow-user-initiated-powerwash",
                                    &allow_user_initiated_powerwash)) {
          LOG(ERROR) << "Invalid JSON string '"
                     << (!error.empty() ? error : value) << "' for policy '"
                     << key::kTPMFirmwareUpdateSettings
                     << "', ignoring. Expected: "
                     << "'{\"allow-user-initiated-powerwash\"=<true/false>}.";
          return;
        }
        em::TPMFirmwareUpdateSettingsProto* tpm_firmware_update_settings =
            policy->mutable_tpm_firmware_update_settings();
        tpm_firmware_update_settings->set_allow_user_initiated_powerwash(
            allow_user_initiated_powerwash);
      });

  EncodeString(
      key::kMinimumRequiredChromeVersion, [policy](const std::string& value) {
        policy->mutable_minimum_required_version()->set_chrome_version(value);
      });

  EncodeBoolean(key::kUnaffiliatedArcAllowed, [policy](bool value) {
    policy->mutable_unaffiliated_arc_allowed()->set_unaffiliated_arc_allowed(
        value);
  });

  EncodeInteger(
      key::kDeviceUserPolicyLoopbackProcessingMode, [policy](int value) {
        policy->mutable_device_user_policy_loopback_processing_mode()->set_mode(
            static_cast<em::DeviceUserPolicyLoopbackProcessingModeProto::Mode>(
                value));
      });

  EncodeString(key::kDeviceLoginScreenIsolateOrigins,
               [policy](const std::string& value) {
                 policy->mutable_device_login_screen_isolate_origins()
                     ->set_isolate_origins(value);
               });

  EncodeBoolean(key::kDeviceLoginScreenSitePerProcess, [policy](bool value) {
    policy->mutable_device_login_screen_site_per_process()
        ->set_site_per_process(value);
  });
}

void DevicePolicyEncoder::EncodeBoolean(
    const char* policy_name, const BooleanPolicyCallback& set_policy) const {
  // Try to get policy value from dict.
  const base::Value* value = dict_->GetValue(policy_name);
  if (!value)
    return;

  // Get actual value, doing type conversion if necessary.
  bool bool_value;
  if (!GetAsBoolean(value, &bool_value)) {
    PrintConversionError(value, "boolean", policy_name);
    return;
  }

  LOG_IF(INFO, log_policy_values_)
      << kColorPolicy << "  " << policy_name << " = "
      << (bool_value ? "true" : "false") << kColorReset;

  // Create proto and set value.
  set_policy(bool_value);
}

void DevicePolicyEncoder::EncodeInteger(
    const char* policy_name, const IntegerPolicyCallback& set_policy) const {
  // Try to get policy value from dict.
  const base::Value* value = dict_->GetValue(policy_name);
  if (!value)
    return;

  // Get actual value, doing type conversion if necessary.
  int int_value;
  if (!GetAsInteger(value, &int_value)) {
    PrintConversionError(value, "integer", policy_name);
    return;
  }

  LOG_IF(INFO, log_policy_values_) << kColorPolicy << "  " << policy_name
                                   << " = " << int_value << kColorReset;

  // Create proto and set value.
  set_policy(int_value);
}

void DevicePolicyEncoder::EncodeString(
    const char* policy_name, const StringPolicyCallback& set_policy) const {
  // Try to get policy value from dict.
  const base::Value* value = dict_->GetValue(policy_name);
  if (!value)
    return;

  // Get actual value, doing type conversion if necessary.
  std::string string_value;
  if (!GetAsString(value, &string_value)) {
    PrintConversionError(value, "string", policy_name);
    return;
  }

  LOG_IF(INFO, log_policy_values_) << kColorPolicy << "  " << policy_name
                                   << " = " << string_value << kColorReset;

  // Create proto and set value.
  set_policy(string_value);
}

void DevicePolicyEncoder::EncodeStringList(
    const char* policy_name, const StringListPolicyCallback& set_policy) const {
  // Try to get policy key from dict.
  const RegistryDict* key = dict_->GetKey(policy_name);
  if (!key)
    return;

  // Get and check all values. Do this in advance to prevent partial writes.
  std::vector<std::string> string_values;
  for (int index = 0; /* empty */; ++index) {
    std::string indexStr = base::IntToString(index + 1);
    const base::Value* value = key->GetValue(indexStr);
    if (!value)
      break;

    std::string string_value;
    if (!GetAsString(value, &string_value)) {
      PrintConversionError(value, "string", policy_name, &indexStr);
      return;
    }

    string_values.push_back(string_value);
  }

  if (log_policy_values_ && LOG_IS_ON(INFO)) {
    LOG(INFO) << kColorPolicy << "  " << policy_name << kColorReset;
    for (const std::string& value : string_values)
      LOG(INFO) << kColorPolicy << "    " << value << kColorReset;
  }

  // Create proto and set values.
  set_policy(string_values);
}

}  // namespace policy
