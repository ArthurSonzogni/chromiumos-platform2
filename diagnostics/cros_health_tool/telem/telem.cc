// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_health_tool/telem/telem.h"

#include <sys/types.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <base/at_exit.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/task/single_thread_task_executor.h>
#include <base/values.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include "diagnostics/cros_healthd_mojo_adapter/cros_healthd_mojo_adapter.h"
#include "diagnostics/mojom/external/network_health.mojom.h"
#include "diagnostics/mojom/external/network_types.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = chromeos::cros_healthd::mojom;
namespace network_config_mojom = chromeos::network_config::mojom;
namespace network_health_mojom = chromeos::network_health::mojom;

constexpr std::pair<const char*, mojom::ProbeCategoryEnum> kCategorySwitches[] =
    {
        {"battery", mojom::ProbeCategoryEnum::kBattery},
        {"storage", mojom::ProbeCategoryEnum::kNonRemovableBlockDevices},
        {"cpu", mojom::ProbeCategoryEnum::kCpu},
        {"timezone", mojom::ProbeCategoryEnum::kTimezone},
        {"memory", mojom::ProbeCategoryEnum::kMemory},
        {"backlight", mojom::ProbeCategoryEnum::kBacklight},
        {"fan", mojom::ProbeCategoryEnum::kFan},
        {"stateful_partition", mojom::ProbeCategoryEnum::kStatefulPartition},
        {"bluetooth", mojom::ProbeCategoryEnum::kBluetooth},
        {"system", mojom::ProbeCategoryEnum::kSystem},
        {"network", mojom::ProbeCategoryEnum::kNetwork},
        {"audio", mojom::ProbeCategoryEnum::kAudio},
        {"boot_performance", mojom::ProbeCategoryEnum::kBootPerformance},
        {"bus", mojom::ProbeCategoryEnum::kBus},
        {"network_interface", mojom::ProbeCategoryEnum::kNetworkInterface},
        {"tpm", mojom::ProbeCategoryEnum::kTpm},
        {"graphics", mojom::ProbeCategoryEnum::kGraphics},
        {"display", mojom::ProbeCategoryEnum::kDisplay},
        {"input", mojom::ProbeCategoryEnum::kInput},
        {"audio_hardware", mojom::ProbeCategoryEnum::kAudioHardware},
        {"sensor", mojom::ProbeCategoryEnum::kSensor},
};

std::string EnumToString(mojom::ProcessState state) {
  switch (state) {
    case mojom::ProcessState::kUnknown:
      return "Unknown";
    case mojom::ProcessState::kRunning:
      return "Running";
    case mojom::ProcessState::kSleeping:
      return "Sleeping";
    case mojom::ProcessState::kWaiting:
      return "Waiting";
    case mojom::ProcessState::kZombie:
      return "Zombie";
    case mojom::ProcessState::kStopped:
      return "Stopped";
    case mojom::ProcessState::kTracingStop:
      return "Tracing Stop";
    case mojom::ProcessState::kDead:
      return "Dead";
    case mojom::ProcessState::kIdle:
      return "Idle";
  }
}

std::string EnumToString(mojom::ErrorType type) {
  switch (type) {
    case mojom::ErrorType::kUnknown:
      return "Unknown Error";
    case mojom::ErrorType::kFileReadError:
      return "File Read Error";
    case mojom::ErrorType::kParseError:
      return "Parse Error";
    case mojom::ErrorType::kSystemUtilityError:
      return "Error running system utility";
    case mojom::ErrorType::kServiceUnavailable:
      return "External service not aviailable";
  }
}

std::string EnumToString(mojom::CpuArchitectureEnum architecture) {
  switch (architecture) {
    case mojom::CpuArchitectureEnum::kUnknown:
      return "unknown";
    case mojom::CpuArchitectureEnum::kX86_64:
      return "x86_64";
    case mojom::CpuArchitectureEnum::kAArch64:
      return "aarch64";
    case mojom::CpuArchitectureEnum::kArmv7l:
      return "armv7l";
  }
}

std::string EnumToString(network_config_mojom::NetworkType type) {
  switch (type) {
    case network_config_mojom::NetworkType::kAll:
      return "Unknown";
    case network_config_mojom::NetworkType::kCellular:
      return "Cellular";
    case network_config_mojom::NetworkType::kEthernet:
      return "Ethernet";
    case network_config_mojom::NetworkType::kMobile:
      return "Mobile";
    case network_config_mojom::NetworkType::kTether:
      return "Tether";
    case network_config_mojom::NetworkType::kVPN:
      return "VPN";
    case network_config_mojom::NetworkType::kWireless:
      return "Wireless";
    case network_config_mojom::NetworkType::kWiFi:
      return "WiFi";
  }
}

std::string EnumToString(network_health_mojom::NetworkState state) {
  switch (state) {
    case network_health_mojom::NetworkState::kUninitialized:
      return "Uninitialized";
    case network_health_mojom::NetworkState::kDisabled:
      return "Disabled";
    case network_health_mojom::NetworkState::kProhibited:
      return "Prohibited";
    case network_health_mojom::NetworkState::kNotConnected:
      return "Not Connected";
    case network_health_mojom::NetworkState::kConnecting:
      return "Connecting";
    case network_health_mojom::NetworkState::kPortal:
      return "Portal";
    case network_health_mojom::NetworkState::kConnected:
      return "Connected";
    case network_health_mojom::NetworkState::kOnline:
      return "Online";
  }
}

std::string EnumToString(network_config_mojom::PortalState state) {
  switch (state) {
    case network_config_mojom::PortalState::kUnknown:
      return "Unknown";
    case network_config_mojom::PortalState::kOnline:
      return "Online";
    case network_config_mojom::PortalState::kPortalSuspected:
      return "Portal Suspected";
    case network_config_mojom::PortalState::kPortal:
      return "Portal Detected";
    case network_config_mojom::PortalState::kProxyAuthRequired:
      return "Proxy Auth Required";
    case network_config_mojom::PortalState::kNoInternet:
      return "No Internet";
  }
}

std::string EnumToString(mojom::EncryptionState encryption_state) {
  switch (encryption_state) {
    case mojom::EncryptionState::kEncryptionDisabled:
      return "Memory encryption disabled";
    case mojom::EncryptionState::kTmeEnabled:
      return "TME enabled";
    case mojom::EncryptionState::kMktmeEnabled:
      return "MKTME enabled";
    case mojom::EncryptionState::kUnknown:
      return "Unknown state";
  }
}

std::string EnumToString(mojom::CryptoAlgorithm algorithm) {
  switch (algorithm) {
    case mojom::CryptoAlgorithm::kAesXts128:
      return "AES-XTS-128";
    case mojom::CryptoAlgorithm::kAesXts256:
      return "AES-XTS-256";
    case mojom::CryptoAlgorithm::kUnknown:
      return "Invalid Algorithm";
  }
}

std::string EnumToString(mojom::BusDeviceClass device_class) {
  switch (device_class) {
    case mojom::BusDeviceClass::kOthers:
      return "others";
    case mojom::BusDeviceClass::kDisplayController:
      return "display controller";
    case mojom::BusDeviceClass::kEthernetController:
      return "ethernet controller";
    case mojom::BusDeviceClass::kWirelessController:
      return "wireless controller";
    case mojom::BusDeviceClass::kBluetoothAdapter:
      return "bluetooth controller";
    case mojom::BusDeviceClass::kThunderboltController:
      return "thunderbolt controller";
    case mojom::BusDeviceClass::kAudioCard:
      return "audio card";
  }
}

// The conversion, except for kUnmappedEnumField, follows the function
// |fwupd_version_format_to_string| in the libfwupd.
std::string EnumToString(mojom::FwupdVersionFormat fwupd_version_format) {
  switch (fwupd_version_format) {
    case mojom::FwupdVersionFormat::kUnmappedEnumField:
      LOG(FATAL) << "Got UnmappedEnumField";
      return "unmapped-enum-field";
    case mojom::FwupdVersionFormat::kUnknown:
      return "unknown";
    case mojom::FwupdVersionFormat::kPlain:
      return "plain";
    case mojom::FwupdVersionFormat::kNumber:
      return "number";
    case mojom::FwupdVersionFormat::kPair:
      return "pair";
    case mojom::FwupdVersionFormat::kTriplet:
      return "triplet";
    case mojom::FwupdVersionFormat::kQuad:
      return "quad";
    case mojom::FwupdVersionFormat::kBcd:
      return "bcd";
    case mojom::FwupdVersionFormat::kIntelMe:
      return "intel-me";
    case mojom::FwupdVersionFormat::kIntelMe2:
      return "intel-me2";
    case mojom::FwupdVersionFormat::kSurfaceLegacy:
      return "surface-legacy";
    case mojom::FwupdVersionFormat::kSurface:
      return "surface";
    case mojom::FwupdVersionFormat::kDellBios:
      return "dell-bios";
    case mojom::FwupdVersionFormat::kHex:
      return "hex";
  }
}

std::string EnumToString(mojom::BootMode mode) {
  switch (mode) {
    case mojom::BootMode::kUnknown:
      return "Unknown";
    case mojom::BootMode::kCrosSecure:
      return "cros_secure";
    case mojom::BootMode::kCrosEfi:
      return "cros_efi";
    case mojom::BootMode::kCrosLegacy:
      return "cros_legacy";
    case mojom::BootMode::kCrosEfiSecure:
      return "cros_efi_secure";
  }
}

std::string EnumToString(mojom::TpmGSCVersion version) {
  switch (version) {
    case mojom::TpmGSCVersion::kNotGSC:
      return "NotGSC";
    case mojom::TpmGSCVersion::kCr50:
      return "Cr50";
    case mojom::TpmGSCVersion::kTi50:
      return "Ti50";
  }
}

std::string EnumToString(mojom::ThunderboltSecurityLevel level) {
  switch (level) {
    case mojom::ThunderboltSecurityLevel::kNone:
      return "None";
    case mojom::ThunderboltSecurityLevel::kUserLevel:
      return "User";
    case mojom::ThunderboltSecurityLevel::kSecureLevel:
      return "Secure";
    case mojom::ThunderboltSecurityLevel::kDpOnlyLevel:
      return "DpOnly";
    case mojom::ThunderboltSecurityLevel::kUsbOnlyLevel:
      return "UsbOnly";
    case mojom::ThunderboltSecurityLevel::kNoPcieLevel:
      return "NoPcie";
  }
}

std::optional<std::string> EnumToString(mojom::BluetoothDeviceType type) {
  switch (type) {
    case mojom::BluetoothDeviceType::kBrEdr:
      return "BR/EDR";
    case mojom::BluetoothDeviceType::kLe:
      return "LE";
    case mojom::BluetoothDeviceType::kDual:
      return "DUAL";
    case mojom::BluetoothDeviceType::kUnfound:
      return std::nullopt;
    case mojom::BluetoothDeviceType::kUnmappedEnumField:
      return std::nullopt;
  }
}

std::string EnumToString(mojom::VulnerabilityInfo::Status status) {
  switch (status) {
    case mojom::VulnerabilityInfo::Status::kUnmappedEnumField:
      LOG(FATAL) << "Got UnmappedEnumField";
      return "UnmappedEnumField";
    case mojom::VulnerabilityInfo::Status::kNotAffected:
      return "Not affected";
    case mojom::VulnerabilityInfo::Status::kVulnerable:
      return "Vulnerable";
    case mojom::VulnerabilityInfo::Status::kMitigation:
      return "Mitigation";
    case mojom::VulnerabilityInfo::Status::kUnknown:
      return "Unknown";
    case mojom::VulnerabilityInfo::Status::kUnrecognized:
      return "Unrecognized";
  }
}

std::string EnumToString(mojom::CpuVirtualizationInfo::Type type) {
  switch (type) {
    case mojom::CpuVirtualizationInfo::Type::kUnmappedEnumField:
      LOG(FATAL) << "Got UnmappedEnumField";
      return "UnmappedEnumField";
    case mojom::CpuVirtualizationInfo::Type::kVMX:
      return "VMX";
    case mojom::CpuVirtualizationInfo::Type::kSVM:
      return "SVM";
  }
}

std::string EnumToString(mojom::VirtualizationInfo::SMTControl control) {
  switch (control) {
    case mojom::VirtualizationInfo::SMTControl::kUnmappedEnumField:
      return "UnmappedEnumField";
    case mojom::VirtualizationInfo::SMTControl::kOn:
      return "on";
    case mojom::VirtualizationInfo::SMTControl::kOff:
      return "off";
    case mojom::VirtualizationInfo::SMTControl::kForceOff:
      return "forceoff";
    case mojom::VirtualizationInfo::SMTControl::kNotSupported:
      return "notsupported";
    case mojom::VirtualizationInfo::SMTControl::kNotImplemented:
      return "notimplemented";
  }
}

std::string EnumToString(mojom::InputDevice::ConnectionType type) {
  switch (type) {
    case mojom::InputDevice::ConnectionType::kUnmappedEnumField:
      LOG(FATAL) << "Got UnmappedEnumField";
      return "UnmappedEnumField";
    case mojom::InputDevice::ConnectionType::kInternal:
      return "Internal";
    case mojom::InputDevice::ConnectionType::kUSB:
      return "USB";
    case mojom::InputDevice::ConnectionType::kBluetooth:
      return "Bluetooth";
    case mojom::InputDevice::ConnectionType::kUnknown:
      return "Unknown";
  }
}

std::optional<std::string> EnumToString(mojom::DisplayInputType type) {
  switch (type) {
    case mojom::DisplayInputType::kDigital:
      return "Digital";
    case mojom::DisplayInputType::kAnalog:
      return "Analog";
    case mojom::DisplayInputType::kUnmappedEnumField:
      return std::nullopt;
  }
}

std::string EnumToString(mojom::OsInfo::EfiPlatformSize size) {
  switch (size) {
    case mojom::OsInfo::EfiPlatformSize::kUnmappedEnumField:
      LOG(FATAL) << "Got UnmappedEnumField";
      return "UnmappedEnumField";
    case mojom::OsInfo::EfiPlatformSize::kUnknown:
      return "unknown";
    case mojom::OsInfo::EfiPlatformSize::k64:
      return "64";
    case mojom::OsInfo::EfiPlatformSize::k32:
      return "32";
  }
}

std::string EnumToString(mojom::Sensor::Type type) {
  switch (type) {
    case mojom::Sensor::Type::kUnmappedEnumField:
      return "UnmappedEnumField";
    case mojom::Sensor::Type::kAccel:
      return "Accel";
    case mojom::Sensor::Type::kLight:
      return "Light";
    case mojom::Sensor::Type::kGyro:
      return "Gyro";
    case mojom::Sensor::Type::kAngle:
      return "Angle";
    case mojom::Sensor::Type::kGravity:
      return "Gravity";
  }
}

std::string EnumToString(mojom::Sensor::Location type) {
  switch (type) {
    case mojom::Sensor::Location::kUnmappedEnumField:
      return "UnmappedEnumField";
    case mojom::Sensor::Location::kUnknown:
      return "Unknown";
    case mojom::Sensor::Location::kBase:
      return "Base";
    case mojom::Sensor::Location::kLid:
      return "Lid";
    case mojom::Sensor::Location::kCamera:
      return "Camera";
  }
}

#define SET_DICT(key, info, output) SetJsonDictValue(#key, info->key, output);

template <typename T>
void SetJsonDictValue(const std::string& key,
                      const T& value,
                      base::Value* output) {
  if constexpr (std::is_same_v<T, uint32_t> || std::is_same_v<T, int64_t> ||
                std::is_same_v<T, uint64_t>) {
    // |base::Value| doesn't support these types, we need to convert them to
    // string.
    SetJsonDictValue(key, std::to_string(value), output);
  } else if constexpr (std::is_same_v<T, std::optional<std::string>>) {
    if (value.has_value())
      SetJsonDictValue(key, value.value(), output);
    // TODO(b/194872701)
    // NOLINTNEXTLINE(readability/braces)
  } else if constexpr (std::is_same_v<
                           T, std::optional<std::vector<std::string>>>) {
    if (value.has_value())
      SetJsonDictValue(key, value.value(), output);
  } else if constexpr (std::is_same_v<T, mojom::NullableDoublePtr>) {
    if (value)
      SetJsonDictValue(key, value->value, output);
  } else if constexpr (std::is_same_v<T, mojom::NullableUint8Ptr>) {
    if (value)
      SetJsonDictValue(key, value->value, output);
  } else if constexpr (std::is_same_v<T, mojom::NullableInt16Ptr>) {
    if (value)
      SetJsonDictValue(key, value->value, output);
  } else if constexpr (std::is_same_v<T, mojom::NullableUint16Ptr>) {
    if (value)
      SetJsonDictValue(key, value->value, output);
  } else if constexpr (std::is_same_v<T, mojom::NullableUint32Ptr>) {
    if (value)
      SetJsonDictValue(key, value->value, output);
  } else if constexpr (std::is_same_v<T, mojom::NullableUint64Ptr>) {
    if (value)
      SetJsonDictValue(key, value->value, output);
    // TODO(b/194872701): This line cannot be broken because the linter issue.
    // clang-format off
  } else if constexpr (std::is_same_v<T, network_health_mojom::UInt32ValuePtr>){
    // clang-format on
    if (value)
      SetJsonDictValue(key, value->value, output);
  } else if constexpr (std::is_enum_v<T>) {
    SetJsonDictValue(key, EnumToString(value), output);
    // TODO(b/194872701)
    // NOLINTNEXTLINE(readability/braces)
  } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
    base::Value* string_vector =
        output->SetKey(key, base::Value{base::Value::Type::LIST});
    for (const auto& s : value)
      string_vector->Append(s);
  } else {
    output->SetKey(key, base::Value(value));
  }
}

void OutputJson(const base::Value& output) {
  std::string json;
  base::JSONWriter::WriteWithOptions(
      output, base::JSONWriter::Options::OPTIONS_PRETTY_PRINT, &json);

  std::cout << json << std::endl;
}

void DisplayError(const mojom::ProbeErrorPtr& error) {
  base::Value output{base::Value::Type::DICTIONARY};
  SET_DICT(type, error, &output);
  SET_DICT(msg, error, &output);

  OutputJson(output);
}

void DisplayProcessInfo(const mojom::ProcessResultPtr& result) {
  if (result.is_null())
    return;

  if (result->is_error()) {
    DisplayError(result->get_error());
    return;
  }

  const auto& info = result->get_process_info();

  base::Value output{base::Value::Type::DICTIONARY};
  SET_DICT(bytes_read, info, &output);
  SET_DICT(bytes_written, info, &output);
  SET_DICT(cancelled_bytes_written, info, &output);
  SET_DICT(command, info, &output);
  SET_DICT(free_memory_kib, info, &output);
  SET_DICT(name, info, &output);
  SET_DICT(nice, info, &output);
  SET_DICT(parent_process_id, info, &output);
  SET_DICT(process_group_id, info, &output);
  SET_DICT(process_id, info, &output);
  SET_DICT(physical_bytes_read, info, &output);
  SET_DICT(physical_bytes_written, info, &output);
  SET_DICT(priority, info, &output);
  SET_DICT(read_system_calls, info, &output);
  SET_DICT(resident_memory_kib, info, &output);
  SET_DICT(state, info, &output);
  SET_DICT(threads, info, &output);
  SET_DICT(total_memory_kib, info, &output);
  SET_DICT(uptime_ticks, info, &output);
  SET_DICT(user_id, info, &output);
  SET_DICT(write_system_calls, info, &output);

  OutputJson(output);
}

void DisplayBatteryInfo(const mojom::BatteryResultPtr& result) {
  if (result->is_error()) {
    DisplayError(result->get_error());
    return;
  }

  const auto& info = result->get_battery_info();
  // There might be no battery if it's AC only.
  // Run the following command on DUT to see if the device is configured to AC
  // only.
  // # cros_config /hardware-properties psu-type
  if (info.is_null()) {
    return;
  }

  base::Value output{base::Value::Type::DICTIONARY};
  SET_DICT(charge_full, info, &output);
  SET_DICT(charge_full_design, info, &output);
  SET_DICT(charge_now, info, &output);
  SET_DICT(current_now, info, &output);
  SET_DICT(cycle_count, info, &output);
  SET_DICT(model_name, info, &output);
  SET_DICT(serial_number, info, &output);
  SET_DICT(status, info, &output);
  SET_DICT(technology, info, &output);
  SET_DICT(vendor, info, &output);
  SET_DICT(voltage_min_design, info, &output);
  SET_DICT(voltage_now, info, &output);

  // Optional fields
  SET_DICT(manufacture_date, info, &output);
  SET_DICT(temperature, info, &output);

  OutputJson(output);
}

void DisplayAudioInfo(const mojom::AudioResultPtr& audio_result) {
  if (audio_result->is_error()) {
    DisplayError(audio_result->get_error());
    return;
  }

  const auto& audio = audio_result->get_audio_info();
  if (audio.is_null()) {
    std::cout << "Device does not have audio info" << std::endl;
    return;
  }

  base::Value output{base::Value::Type::DICTIONARY};
  output.SetStringKey("input_device_name", audio->input_device_name);
  output.SetStringKey("output_device_name", audio->output_device_name);
  output.SetBoolKey("input_mute", audio->input_mute);
  output.SetBoolKey("output_mute", audio->output_mute);
  output.SetIntKey("input_gain", audio->input_gain);
  output.SetIntKey("output_volume", audio->output_volume);
  output.SetIntKey("severe_underruns", audio->severe_underruns);
  output.SetIntKey("underruns", audio->underruns);

  OutputJson(output);
}

void DisplayDisplayInfo(const mojom::DisplayResultPtr& display_result) {
  if (display_result->is_error()) {
    DisplayError(display_result->get_error());
    return;
  }

  const auto& display = display_result->get_display_info();
  if (display.is_null()) {
    std::cout << "Device does not have display info" << std::endl;
    return;
  }

  const auto& edp_info = display->edp_info;
  base::Value output{base::Value::Type::DICTIONARY};
  auto* edp = output.SetKey("edp", base::Value{base::Value::Type::DICTIONARY});
  SET_DICT(privacy_screen_supported, edp_info, edp);
  SET_DICT(privacy_screen_enabled, edp_info, edp);
  SET_DICT(display_width, edp_info, edp);
  SET_DICT(display_height, edp_info, edp);
  SET_DICT(resolution_horizontal, edp_info, edp);
  SET_DICT(resolution_vertical, edp_info, edp);
  SET_DICT(refresh_rate, edp_info, edp);
  SET_DICT(manufacturer, edp_info, edp);
  SET_DICT(model_id, edp_info, edp);
  SET_DICT(serial_number, edp_info, edp);
  SET_DICT(manufacture_week, edp_info, edp);
  SET_DICT(manufacture_year, edp_info, edp);
  SET_DICT(edid_version, edp_info, edp);
  SET_DICT(input_type, edp_info, edp);
  SET_DICT(display_name, edp_info, edp);

  if (display->dp_infos) {
    const auto& dp_infos = display->dp_infos;
    auto* dp = output.SetKey("dp", base::Value{base::Value::Type::LIST});
    for (const auto& dp_info : *dp_infos) {
      base::Value data{base::Value::Type::DICTIONARY};
      SET_DICT(display_width, dp_info, &data);
      SET_DICT(display_height, dp_info, &data);
      SET_DICT(resolution_horizontal, dp_info, &data);
      SET_DICT(resolution_vertical, dp_info, &data);
      SET_DICT(refresh_rate, dp_info, &data);
      SET_DICT(manufacturer, dp_info, &data);
      SET_DICT(model_id, dp_info, &data);
      SET_DICT(serial_number, dp_info, &data);
      SET_DICT(manufacture_week, dp_info, &data);
      SET_DICT(manufacture_year, dp_info, &data);
      SET_DICT(edid_version, dp_info, &data);
      SET_DICT(input_type, dp_info, &data);
      SET_DICT(display_name, dp_info, &data);
      dp->Append(std::move(data));
    }
  }

  OutputJson(output);
}

void DisplayBootPerformanceInfo(const mojom::BootPerformanceResultPtr& result) {
  if (result->is_error()) {
    DisplayError(result->get_error());
    return;
  }

  const auto& info = result->get_boot_performance_info();
  CHECK(!info.is_null());

  base::Value output{base::Value::Type::DICTIONARY};
  output.SetStringKey("shutdown_reason", info->shutdown_reason);
  output.SetDoubleKey("boot_up_seconds", info->boot_up_seconds);
  output.SetDoubleKey("boot_up_timestamp", info->boot_up_timestamp);
  output.SetDoubleKey("shutdown_seconds", info->shutdown_seconds);
  output.SetDoubleKey("shutdown_timestamp", info->shutdown_timestamp);

  OutputJson(output);
}

void DisplayBlockDeviceInfo(
    const mojom::NonRemovableBlockDeviceResultPtr& result) {
  if (result->is_error()) {
    DisplayError(result->get_error());
    return;
  }

  const auto& infos = result->get_block_device_info();

  base::Value output{base::Value::Type::DICTIONARY};
  auto* block_devices =
      output.SetKey("block_devices", base::Value{base::Value::Type::LIST});
  for (const auto& info : infos) {
    base::Value data{base::Value::Type::DICTIONARY};
    SET_DICT(bytes_read_since_last_boot, info, &data);
    SET_DICT(bytes_written_since_last_boot, info, &data);
    SET_DICT(io_time_seconds_since_last_boot, info, &data);
    SET_DICT(name, info, &data);
    SET_DICT(path, info, &data);
    SET_DICT(read_time_seconds_since_last_boot, info, &data);
    SET_DICT(serial, info, &data);
    SET_DICT(size, info, &data);
    SET_DICT(type, info, &data);
    SET_DICT(write_time_seconds_since_last_boot, info, &data);
    SET_DICT(manufacturer_id, info, &data);

    // optional field
    SET_DICT(discard_time_seconds_since_last_boot, info, &data);

    block_devices->Append(std::move(data));
  }

  OutputJson(output);
}

void DisplayBluetoothInfo(const mojom::BluetoothResultPtr& result) {
  if (result->is_error()) {
    DisplayError(result->get_error());
    return;
  }

  const auto& infos = result->get_bluetooth_adapter_info();

  base::Value output{base::Value::Type::DICTIONARY};
  auto* adapters =
      output.SetKey("adapters", base::Value{base::Value::Type::LIST});
  for (const auto& info : infos) {
    base::Value data{base::Value::Type::DICTIONARY};
    SET_DICT(address, info, &data);
    SET_DICT(name, info, &data);
    SET_DICT(num_connected_devices, info, &data);
    SET_DICT(powered, info, &data);
    auto* connected_devices =
        data.SetKey("connected_devices", base::Value{base::Value::Type::LIST});
    if (info->connected_devices.has_value()) {
      for (const auto& device : info->connected_devices.value()) {
        base::Value device_data{base::Value::Type::DICTIONARY};
        SET_DICT(address, device, &device_data);
        SET_DICT(name, device, &device_data);
        SET_DICT(type, device, &device_data);
        SET_DICT(appearance, device, &device_data);
        SET_DICT(modalias, device, &device_data);
        SET_DICT(rssi, device, &device_data);
        SET_DICT(mtu, device, &device_data);
        SET_DICT(uuids, device, &device_data);
        SET_DICT(battery_percentage, device, &device_data);
        connected_devices->Append(std::move(device_data));
      }
    }
    SET_DICT(discoverable, info, &data);
    SET_DICT(discovering, info, &data);
    SET_DICT(uuids, info, &data);
    SET_DICT(modalias, info, &data);
    SET_DICT(service_allow_list, info, &data);
    if (info->supported_capabilities) {
      auto* out_capabilities = data.SetKey(
          "supported_capabilities", base::Value{base::Value::Type::DICTIONARY});
      SET_DICT(max_adv_len, info->supported_capabilities, out_capabilities);
      SET_DICT(max_scn_rsp_len, info->supported_capabilities, out_capabilities);
      SET_DICT(min_tx_power, info->supported_capabilities, out_capabilities);
      SET_DICT(max_tx_power, info->supported_capabilities, out_capabilities);
    }
    adapters->Append(std::move(data));
  }

  OutputJson(output);
}

void DisplayCpuInfo(const mojom::CpuResultPtr& result) {
  if (result->is_error()) {
    DisplayError(result->get_error());
    return;
  }

  const auto& info = result->get_cpu_info();

  LOG(INFO) << "Fetcher value: " << info->virtualization->has_kvm_device;

  base::Value output{base::Value::Type::DICTIONARY};
  auto* physical_cpus =
      output.SetKey("physical_cpus", base::Value{base::Value::Type::LIST});
  for (const auto& physical_cpu : info->physical_cpus) {
    base::Value physical_cpu_data{base::Value::Type::DICTIONARY};
    auto* logical_cpus = physical_cpu_data.SetKey(
        "logical_cpus", base::Value{base::Value::Type::LIST});
    for (const auto& logical_cpu : physical_cpu->logical_cpus) {
      base::Value logical_cpu_data{base::Value::Type::DICTIONARY};

      SET_DICT(idle_time_user_hz, logical_cpu, &logical_cpu_data);
      SET_DICT(max_clock_speed_khz, logical_cpu, &logical_cpu_data);
      SET_DICT(scaling_current_frequency_khz, logical_cpu, &logical_cpu_data);
      SET_DICT(scaling_max_frequency_khz, logical_cpu, &logical_cpu_data);
      SET_DICT(system_time_user_hz, logical_cpu, &logical_cpu_data);
      SET_DICT(user_time_user_hz, logical_cpu, &logical_cpu_data);

      auto* c_states = logical_cpu_data.SetKey(
          "c_states", base::Value{base::Value::Type::LIST});
      for (const auto& c_state : logical_cpu->c_states) {
        base::Value c_state_data{base::Value::Type::DICTIONARY};

        SET_DICT(name, c_state, &c_state_data);
        SET_DICT(time_in_state_since_last_boot_us, c_state, &c_state_data);

        c_states->Append(std::move(c_state_data));
      }

      logical_cpus->Append(std::move(logical_cpu_data));
    }
    if (physical_cpu->flags) {
      auto* cpu_flags = physical_cpu_data.SetKey(
          "flags", base::Value{base::Value::Type::LIST});
      for (const auto& flag : *(physical_cpu->flags)) {
        cpu_flags->Append(std::move(flag));
      }
    }

    if (!physical_cpu->virtualization.is_null()) {
      auto* cpu_virtualization_info = physical_cpu_data.SetKey(
          "cpu_virtualization", base::Value{base::Value::Type::DICTIONARY});
      SET_DICT(type, physical_cpu->virtualization, cpu_virtualization_info);
      SET_DICT(is_enabled, physical_cpu->virtualization,
               cpu_virtualization_info);
      SET_DICT(is_locked, physical_cpu->virtualization,
               cpu_virtualization_info);
    }

    // Optional field
    SET_DICT(model_name, physical_cpu, &physical_cpu_data);

    physical_cpus->Append(std::move(physical_cpu_data));
  }

  auto* temperature_channels = output.SetKey(
      "temperature_channels", base::Value{base::Value::Type::LIST});
  for (const auto& channel : info->temperature_channels) {
    base::Value data{base::Value::Type::DICTIONARY};

    SET_DICT(temperature_celsius, channel, &data);

    // Optional field
    SET_DICT(label, channel, &data);

    temperature_channels->Append(std::move(data));
  }

  SET_DICT(num_total_threads, info, &output);
  SET_DICT(architecture, info, &output);

  auto* vulnerabilities = output.SetKey(
      "vulnerabilities", base::Value{base::Value::Type::DICTIONARY});
  for (const auto& vulnerability_key_value : *(info->vulnerabilities)) {
    auto* vulnerability =
        vulnerabilities->SetKey(vulnerability_key_value.first,
                                base::Value{base::Value::Type::DICTIONARY});
    SET_DICT(status, vulnerability_key_value.second, vulnerability);
    SET_DICT(message, vulnerability_key_value.second, vulnerability);
  }

  if (info->virtualization) {
    auto* virtualization_info = output.SetKey(
        "virtualization", base::Value{base::Value::Type::DICTIONARY});
    SET_DICT(has_kvm_device, info->virtualization, virtualization_info);
    SET_DICT(is_smt_active, info->virtualization, virtualization_info);
    SET_DICT(smt_control, info->virtualization, virtualization_info);
  }

  if (info->keylocker_info) {
    auto* out_keylocker = output.SetKey(
        "keylocker_info", base::Value{base::Value::Type::DICTIONARY});
    SET_DICT(keylocker_configured, info->keylocker_info, out_keylocker);
  }
  OutputJson(output);
}

void DisplayFanInfo(const mojom::FanResultPtr& result) {
  if (result->is_error()) {
    DisplayError(result->get_error());
    return;
  }

  const auto& infos = result->get_fan_info();

  base::Value output{base::Value::Type::DICTIONARY};
  auto* fans = output.SetKey("fans", base::Value{base::Value::Type::LIST});
  for (const auto& info : infos) {
    base::Value data{base::Value::Type::DICTIONARY};
    SET_DICT(speed_rpm, info, &data);

    fans->Append(std::move(data));
  }

  OutputJson(output);
}

void DisplayNetworkInfo(const mojom::NetworkResultPtr& result) {
  if (result->is_error()) {
    DisplayError(result->get_error());
    return;
  }

  const auto& infos = result->get_network_health()->networks;

  base::Value output{base::Value::Type::DICTIONARY};
  auto* networks =
      output.SetKey("networks", base::Value{base::Value::Type::LIST});
  for (const auto& info : infos) {
    base::Value data{base::Value::Type::DICTIONARY};
    SET_DICT(portal_state, info, &data);
    SET_DICT(state, info, &data);
    SET_DICT(type, info, &data);

    // Optional fields
    SET_DICT(guid, info, &data);
    SET_DICT(name, info, &data);
    SET_DICT(mac_address, info, &data);
    SET_DICT(ipv4_address, info, &data);
    SET_DICT(signal_strength, info, &data);
    if (info->signal_strength_stats) {
      auto* stats = data.SetKey("signal_strength_stats",
                                base::Value{base::Value::Type::DICTIONARY});
      SET_DICT(average, info->signal_strength_stats, stats);
      SET_DICT(deviation, info->signal_strength_stats, stats);
    }
    if (info->ipv6_addresses.size()) {
      SetJsonDictValue("ipv6_addresses",
                       base::JoinString(info->ipv6_addresses, ":"), &data);
    }

    networks->Append(std::move(data));
  }

  OutputJson(output);
}

void DisplayNetworkInterfaceInfo(
    const mojom::NetworkInterfaceResultPtr& result) {
  if (result->is_error()) {
    DisplayError(result->get_error());
    return;
  }

  const auto& infos = result->get_network_interface_info();

  base::Value output{base::Value::Type::DICTIONARY};
  auto* out_network_interfaces =
      output.SetKey("network_interfaces", base::Value{base::Value::Type::LIST});

  for (const auto& network_interface : infos) {
    base::Value out_network_interface{base::Value::Type::DICTIONARY};
    switch (network_interface->which()) {
      case mojom::NetworkInterfaceInfo::Tag::kWirelessInterfaceInfo: {
        const auto& wireless_interface =
            network_interface->get_wireless_interface_info();
        auto* out_wireless_interface = out_network_interface.SetKey(
            "wireless_interface", base::Value{base::Value::Type::DICTIONARY});
        base::Value data{base::Value::Type::DICTIONARY};
        SET_DICT(interface_name, wireless_interface, out_wireless_interface);
        SET_DICT(power_management_on, wireless_interface,
                 out_wireless_interface);
        const auto& link_info = wireless_interface->wireless_link_info;
        if (link_info) {
          auto* out_link = out_wireless_interface->SetKey(
              "link_info", base::Value{base::Value::Type::DICTIONARY});
          SET_DICT(access_point_address_str, link_info, out_link);
          SET_DICT(tx_bit_rate_mbps, link_info, out_link);
          SET_DICT(rx_bit_rate_mbps, link_info, out_link);
          SET_DICT(tx_power_dBm, link_info, out_link);
          SET_DICT(encyption_on, link_info, out_link);
          SET_DICT(link_quality, link_info, out_link);
          SET_DICT(signal_level_dBm, link_info, out_link);
        }
        break;
      }
    }
    out_network_interfaces->Append(std::move(out_network_interface));
  }

  OutputJson(output);
}

void DisplayTimezoneInfo(const mojom::TimezoneResultPtr& result) {
  if (result->is_error()) {
    DisplayError(result->get_error());
    return;
  }

  const auto& info = result->get_timezone_info();
  CHECK(!info.is_null());

  base::Value output{base::Value::Type::DICTIONARY};
  output.SetStringKey("posix", info->posix);
  output.SetStringKey("region", info->region);

  OutputJson(output);
}

void DisplayMemoryInfo(const mojom::MemoryResultPtr& result) {
  if (result->is_error()) {
    DisplayError(result->get_error());
    return;
  }

  const auto& info = result->get_memory_info();
  CHECK(!info.is_null());

  base::Value output{base::Value::Type::DICTIONARY};
  SET_DICT(available_memory_kib, info, &output);
  SET_DICT(free_memory_kib, info, &output);
  SET_DICT(page_faults_since_last_boot, info, &output);
  SET_DICT(total_memory_kib, info, &output);

  const auto& memory_encryption_info = info->memory_encryption_info;
  if (memory_encryption_info) {
    auto* out_mem_encryption = output.SetKey(
        "memory_encryption_info", base::Value{base::Value::Type::DICTIONARY});
    SET_DICT(encryption_state, memory_encryption_info, out_mem_encryption);
    SET_DICT(max_key_number, memory_encryption_info, out_mem_encryption);
    SET_DICT(key_length, memory_encryption_info, out_mem_encryption);
    SET_DICT(active_algorithm, memory_encryption_info, out_mem_encryption);
  }

  OutputJson(output);
}

void DisplayBacklightInfo(const mojom::BacklightResultPtr& result) {
  if (result->is_error()) {
    DisplayError(result->get_error());
    return;
  }

  const auto& infos = result->get_backlight_info();

  base::Value output{base::Value::Type::DICTIONARY};
  auto* backlights =
      output.SetKey("backlights", base::Value{base::Value::Type::LIST});
  for (const auto& info : infos) {
    base::Value data{base::Value::Type::DICTIONARY};
    SET_DICT(brightness, info, &data);
    SET_DICT(max_brightness, info, &data);
    SET_DICT(path, info, &data);

    backlights->Append(std::move(data));
  }

  OutputJson(output);
}

void DisplayStatefulPartitionInfo(
    const mojom::StatefulPartitionResultPtr& result) {
  if (result->is_error()) {
    DisplayError(result->get_error());
    return;
  }

  const auto& info = result->get_partition_info();
  CHECK(!info.is_null());

  base::Value output{base::Value::Type::DICTIONARY};
  SET_DICT(available_space, info, &output);
  SET_DICT(filesystem, info, &output);
  SET_DICT(mount_source, info, &output);
  SET_DICT(total_space, info, &output);

  OutputJson(output);
}

void DisplaySystemInfo(const mojom::SystemResultPtr& system_result) {
  if (system_result->is_error()) {
    DisplayError(system_result->get_error());
    return;
  }
  const auto& system_info = system_result->get_system_info();
  base::Value output{base::Value::Type::DICTIONARY};

  const auto& os_info = system_info->os_info;
  auto* out_os_info =
      output.SetKey("os_info", base::Value{base::Value::Type::DICTIONARY});
  SET_DICT(code_name, os_info, out_os_info);
  SET_DICT(marketing_name, os_info, out_os_info);
  SET_DICT(oem_name, os_info, out_os_info);
  SET_DICT(boot_mode, os_info, out_os_info);
  SET_DICT(efi_platform_size, os_info, out_os_info);

  const auto& os_version = os_info->os_version;
  auto* out_os_version = out_os_info->SetKey(
      "os_version", base::Value{base::Value::Type::DICTIONARY});
  SET_DICT(release_milestone, os_version, out_os_version);
  SET_DICT(build_number, os_version, out_os_version);
  SET_DICT(patch_number, os_version, out_os_version);
  SET_DICT(release_channel, os_version, out_os_version);

  const auto& vpd_info = system_info->vpd_info;
  if (vpd_info) {
    auto* out_vpd_info =
        output.SetKey("vpd_info", base::Value{base::Value::Type::DICTIONARY});
    SET_DICT(serial_number, vpd_info, out_vpd_info);
    SET_DICT(region, vpd_info, out_vpd_info);
    SET_DICT(mfg_date, vpd_info, out_vpd_info);
    SET_DICT(activate_date, vpd_info, out_vpd_info);
    SET_DICT(sku_number, vpd_info, out_vpd_info);
    SET_DICT(model_name, vpd_info, out_vpd_info);
  }

  const auto& dmi_info = system_info->dmi_info;
  if (dmi_info) {
    auto* out_dmi_info =
        output.SetKey("dmi_info", base::Value{base::Value::Type::DICTIONARY});
    SET_DICT(bios_vendor, dmi_info, out_dmi_info);
    SET_DICT(bios_version, dmi_info, out_dmi_info);
    SET_DICT(board_name, dmi_info, out_dmi_info);
    SET_DICT(board_vendor, dmi_info, out_dmi_info);
    SET_DICT(board_version, dmi_info, out_dmi_info);
    SET_DICT(chassis_vendor, dmi_info, out_dmi_info);
    SET_DICT(chassis_type, dmi_info, out_dmi_info);
    SET_DICT(product_family, dmi_info, out_dmi_info);
    SET_DICT(product_name, dmi_info, out_dmi_info);
    SET_DICT(product_version, dmi_info, out_dmi_info);
    SET_DICT(sys_vendor, dmi_info, out_dmi_info);
  }

  OutputJson(output);
}

base::Value GetBusDeviceJson(const mojom::BusDevicePtr& device) {
  base::Value out_device{base::Value::Type::DICTIONARY};
  SET_DICT(vendor_name, device, &out_device);
  SET_DICT(product_name, device, &out_device);
  SET_DICT(device_class, device, &out_device);
  auto* out_bus_info =
      out_device.SetKey("bus_info", base::Value{base::Value::Type::DICTIONARY});
  switch (device->bus_info->which()) {
    case mojom::BusInfo::Tag::kPciBusInfo: {
      auto* out_pci_info = out_bus_info->SetKey(
          "pci_bus_info", base::Value{base::Value::Type::DICTIONARY});
      const auto& pci_info = device->bus_info->get_pci_bus_info();
      SET_DICT(class_id, pci_info, out_pci_info);
      SET_DICT(subclass_id, pci_info, out_pci_info);
      SET_DICT(prog_if_id, pci_info, out_pci_info);
      SET_DICT(vendor_id, pci_info, out_pci_info);
      SET_DICT(device_id, pci_info, out_pci_info);
      SET_DICT(driver, pci_info, out_pci_info);
      break;
    }
    case mojom::BusInfo::Tag::kUsbBusInfo: {
      const auto& usb_info = device->bus_info->get_usb_bus_info();
      auto* out_usb_info = out_bus_info->SetKey(
          "usb_bus_info", base::Value{base::Value::Type::DICTIONARY});
      SET_DICT(class_id, usb_info, out_usb_info);
      SET_DICT(subclass_id, usb_info, out_usb_info);
      SET_DICT(protocol_id, usb_info, out_usb_info);
      SET_DICT(vendor_id, usb_info, out_usb_info);
      SET_DICT(product_id, usb_info, out_usb_info);
      auto* out_usb_ifs = out_usb_info->SetKey(
          "interfaces", base::Value{base::Value::Type::LIST});
      for (const auto& usb_if_info : usb_info->interfaces) {
        base::Value out_usb_if{base::Value::Type::DICTIONARY};
        SET_DICT(interface_number, usb_if_info, &out_usb_if);
        SET_DICT(class_id, usb_if_info, &out_usb_if);
        SET_DICT(subclass_id, usb_if_info, &out_usb_if);
        SET_DICT(protocol_id, usb_if_info, &out_usb_if);
        SET_DICT(driver, usb_if_info, &out_usb_if);
        out_usb_ifs->Append(std::move(out_usb_if));
      }
      if (usb_info->fwupd_firmware_version_info) {
        auto* out_usb_firmware =
            out_usb_info->SetKey("fwupd_firmware_version_info",
                                 base::Value{base::Value::Type::DICTIONARY});
        SET_DICT(version, usb_info->fwupd_firmware_version_info,
                 out_usb_firmware);
        SET_DICT(version_format, usb_info->fwupd_firmware_version_info,
                 out_usb_firmware);
      }
      break;
    }
    case mojom::BusInfo::Tag::kThunderboltBusInfo: {
      const auto& thunderbolt_info =
          device->bus_info->get_thunderbolt_bus_info();
      auto* out_thunderbolt_info = out_bus_info->SetKey(
          "thunderbolt_bus_info", base::Value{base::Value::Type::DICTIONARY});
      SET_DICT(security_level, thunderbolt_info, out_thunderbolt_info);
      auto* out_thunderbolt_interfaces = out_thunderbolt_info->SetKey(
          "thunderbolt_interfaces", base::Value{base::Value::Type::LIST});
      for (const auto& thunderbolt_interface :
           thunderbolt_info->thunderbolt_interfaces) {
        base::Value out_thunderbolt_interface{base::Value::Type::DICTIONARY};
        SET_DICT(vendor_name, thunderbolt_interface,
                 &out_thunderbolt_interface);
        SET_DICT(device_name, thunderbolt_interface,
                 &out_thunderbolt_interface);
        SET_DICT(device_type, thunderbolt_interface,
                 &out_thunderbolt_interface);
        SET_DICT(device_uuid, thunderbolt_interface,
                 &out_thunderbolt_interface);
        SET_DICT(tx_speed_gbs, thunderbolt_interface,
                 &out_thunderbolt_interface);
        SET_DICT(rx_speed_gbs, thunderbolt_interface,
                 &out_thunderbolt_interface);
        SET_DICT(authorized, thunderbolt_interface, &out_thunderbolt_interface);
        SET_DICT(device_fw_version, thunderbolt_interface,
                 &out_thunderbolt_interface);
        out_thunderbolt_interfaces->Append(
            std::move(out_thunderbolt_interface));
      }
      break;
    }
    case mojom::BusInfo::Tag::kUnmappedField: {
      NOTREACHED();
      break;
    }
  }
  return out_device;
}

void DisplayBusDevices(const mojom::BusResultPtr& bus_result) {
  if (bus_result->is_error()) {
    DisplayError(bus_result->get_error());
    return;
  }

  const auto& devices = bus_result->get_bus_devices();

  base::Value output{base::Value::Type::DICTIONARY};
  auto* out_devices =
      output.SetKey("devices", base::Value{base::Value::Type::LIST});
  for (const auto& device : devices) {
    out_devices->Append(GetBusDeviceJson(device));
  }

  OutputJson(output);
}

void DisplayTpmInfo(const mojom::TpmResultPtr& result) {
  if (result->is_error()) {
    DisplayError(result->get_error());
    return;
  }

  const auto& info = result->get_tpm_info();
  base::Value output{base::Value::Type::DICTIONARY};

  const auto& version = info->version;
  auto* out_version =
      output.SetKey("version", base::Value{base::Value::Type::DICTIONARY});
  SET_DICT(gsc_version, version, out_version);
  SET_DICT(family, version, out_version);
  SET_DICT(spec_level, version, out_version);
  SET_DICT(manufacturer, version, out_version);
  SET_DICT(tpm_model, version, out_version);
  SET_DICT(firmware_version, version, out_version);
  SET_DICT(vendor_specific, version, out_version);

  const auto& status = info->status;
  auto* out_status =
      output.SetKey("status", base::Value{base::Value::Type::DICTIONARY});
  SET_DICT(enabled, status, out_status);
  SET_DICT(owned, status, out_status);
  SET_DICT(owner_password_is_present, status, out_status);

  const auto& dictionary_attack = info->dictionary_attack;
  auto* out_dictionary_attack = output.SetKey(
      "dictionary_attack", base::Value{base::Value::Type::DICTIONARY});
  SET_DICT(counter, dictionary_attack, out_dictionary_attack);
  SET_DICT(threshold, dictionary_attack, out_dictionary_attack);
  SET_DICT(lockout_in_effect, dictionary_attack, out_dictionary_attack);
  SET_DICT(lockout_seconds_remaining, dictionary_attack, out_dictionary_attack);

  const auto& attestation = info->attestation;
  auto* out_attestation =
      output.SetKey("attestation", base::Value{base::Value::Type::DICTIONARY});
  SET_DICT(prepared_for_enrollment, attestation, out_attestation);
  SET_DICT(enrolled, attestation, out_attestation);

  const auto& supported_features = info->supported_features;
  auto* out_supported_features = output.SetKey(
      "supported_features", base::Value{base::Value::Type::DICTIONARY});
  SET_DICT(support_u2f, supported_features, out_supported_features);
  SET_DICT(support_pinweaver, supported_features, out_supported_features);
  SET_DICT(support_runtime_selection, supported_features,
           out_supported_features);
  SET_DICT(is_allowed, supported_features, out_supported_features);

  SET_DICT(did_vid, info, &output);

  OutputJson(output);
}

void DisplayGraphicsInfo(const mojom::GraphicsResultPtr& graphics_result) {
  if (graphics_result->is_error()) {
    DisplayError(graphics_result->get_error());
    return;
  }

  const auto& info = graphics_result->get_graphics_info();
  CHECK(!info.is_null());

  base::Value output{base::Value::Type::DICTIONARY};
  const auto& gles_info = info->gles_info;
  auto* out_gles_info =
      output.SetKey("gles_info", base::Value{base::Value::Type::DICTIONARY});
  SET_DICT(version, gles_info, out_gles_info);
  SET_DICT(shading_version, gles_info, out_gles_info);
  SET_DICT(vendor, gles_info, out_gles_info);
  SET_DICT(renderer, gles_info, out_gles_info);
  SET_DICT(extensions, gles_info, out_gles_info);

  const auto& egl_info = info->egl_info;
  auto* out_egl_info =
      output.SetKey("egl_info", base::Value{base::Value::Type::DICTIONARY});
  SET_DICT(version, egl_info, out_egl_info);
  SET_DICT(vendor, egl_info, out_egl_info);
  SET_DICT(client_api, egl_info, out_egl_info);
  SET_DICT(extensions, egl_info, out_egl_info);

  OutputJson(output);
}

void DisplayInputInfo(const mojom::InputResultPtr& input_result) {
  if (input_result->is_error()) {
    DisplayError(input_result->get_error());
    return;
  }

  const auto& info = input_result->get_input_info();
  CHECK(!info.is_null());

  base::Value output{base::Value::Type::DICTIONARY};
  SET_DICT(touchpad_library_name, info, &output);

  auto* out_touchscreen_devices = output.SetKey(
      "touchscreen_devices", base::Value{base::Value::Type::LIST});
  for (const auto& touchscreen_device : info->touchscreen_devices) {
    base::Value out_touchscreen_device{base::Value::Type::DICTIONARY};
    SET_DICT(touch_points, touchscreen_device, &out_touchscreen_device);
    SET_DICT(has_stylus, touchscreen_device, &out_touchscreen_device);
    SET_DICT(has_stylus_garage_switch, touchscreen_device,
             &out_touchscreen_device);

    auto* out_input_device = out_touchscreen_device.SetKey(
        "input_device", base::Value{base::Value::Type::DICTIONARY});
    SET_DICT(name, touchscreen_device->input_device, out_input_device);
    SET_DICT(connection_type, touchscreen_device->input_device,
             out_input_device);
    SET_DICT(physical_location, touchscreen_device->input_device,
             out_input_device);
    SET_DICT(is_enabled, touchscreen_device->input_device, out_input_device);

    out_touchscreen_devices->Append(std::move(out_touchscreen_device));
  }

  OutputJson(output);
}

void DisplayAudioHardwareInfo(const mojom::AudioHardwareResultPtr& result) {
  if (result->is_error()) {
    DisplayError(result->get_error());
    return;
  }

  base::Value output{base::Value::Type::DICTIONARY};
  const auto& info = result->get_audio_hardware_info();
  CHECK(!info.is_null());

  const auto& audio_cards = info->audio_cards;
  auto* out_audio_cards =
      output.SetKey("audio_cards", base::Value{base::Value::Type::LIST});
  for (const auto& audio_card : audio_cards) {
    base::Value out_audio_card{base::Value::Type::DICTIONARY};
    SET_DICT(alsa_id, audio_card, &out_audio_card);

    if (audio_card->bus_device) {
      out_audio_card.SetKey("bus_device",
                            GetBusDeviceJson(audio_card->bus_device));
    }

    const auto& hd_audio_codecs = audio_card->hd_audio_codecs;
    auto* out_hd_audio_codecs = out_audio_card.SetKey(
        "hd_audio_codecs", base::Value{base::Value::Type::LIST});
    for (const auto& hd_audio_codec : hd_audio_codecs) {
      base::Value out_hd_audio_codec{base::Value::Type::DICTIONARY};
      SET_DICT(name, hd_audio_codec, &out_hd_audio_codec);
      SET_DICT(address, hd_audio_codec, &out_hd_audio_codec);

      out_hd_audio_codecs->Append(std::move(out_hd_audio_codec));
    }

    out_audio_cards->Append(std::move(out_audio_card));
  }

  OutputJson(output);
}

void DisplaySensorInfo(const mojom::SensorResultPtr& result) {
  if (result->is_error()) {
    DisplayError(result->get_error());
    return;
  }

  base::Value output{base::Value::Type::DICTIONARY};
  const auto& info = result->get_sensor_info();
  CHECK(!info.is_null());

  if (info->sensors.has_value()) {
    auto* out_sensors =
        output.SetKey("sensors", base::Value{base::Value::Type::LIST});
    for (const auto& sensor : info->sensors.value()) {
      base::Value out_sensor{base::Value::Type::DICTIONARY};
      SET_DICT(name, sensor, &out_sensor);
      SET_DICT(device_id, sensor, &out_sensor);
      SET_DICT(type, sensor, &out_sensor);
      SET_DICT(location, sensor, &out_sensor);
      out_sensors->Append(std::move(out_sensor));
    }
  }

  SET_DICT(lid_angle, info, &output);

  OutputJson(output);
}

// Displays the retrieved telemetry information to the console.
void DisplayTelemetryInfo(const mojom::TelemetryInfoPtr& info) {
  const auto& battery_result = info->battery_result;
  if (battery_result)
    DisplayBatteryInfo(battery_result);

  const auto& block_device_result = info->block_device_result;
  if (block_device_result)
    DisplayBlockDeviceInfo(block_device_result);

  const auto& cpu_result = info->cpu_result;
  if (cpu_result)
    DisplayCpuInfo(cpu_result);

  const auto& timezone_result = info->timezone_result;
  if (timezone_result)
    DisplayTimezoneInfo(timezone_result);

  const auto& memory_result = info->memory_result;
  if (memory_result)
    DisplayMemoryInfo(memory_result);

  const auto& backlight_result = info->backlight_result;
  if (backlight_result)
    DisplayBacklightInfo(backlight_result);

  const auto& fan_result = info->fan_result;
  if (fan_result)
    DisplayFanInfo(fan_result);

  const auto& stateful_partition_result = info->stateful_partition_result;
  if (stateful_partition_result)
    DisplayStatefulPartitionInfo(stateful_partition_result);

  const auto& bluetooth_result = info->bluetooth_result;
  if (bluetooth_result)
    DisplayBluetoothInfo(bluetooth_result);

  const auto& network_result = info->network_result;
  if (network_result)
    DisplayNetworkInfo(network_result);

  const auto& audio_result = info->audio_result;
  if (audio_result)
    DisplayAudioInfo(audio_result);

  const auto& boot_performance_result = info->boot_performance_result;
  if (boot_performance_result)
    DisplayBootPerformanceInfo(boot_performance_result);

  const auto& network_interface_result = info->network_interface_result;
  if (network_interface_result)
    DisplayNetworkInterfaceInfo(network_interface_result);

  const auto& bus_result = info->bus_result;
  if (bus_result)
    DisplayBusDevices(bus_result);

  const auto& tpm_result = info->tpm_result;
  if (tpm_result)
    DisplayTpmInfo(tpm_result);

  const auto& system_result = info->system_result;
  if (system_result)
    DisplaySystemInfo(system_result);

  const auto& graphics_result = info->graphics_result;
  if (graphics_result)
    DisplayGraphicsInfo(graphics_result);

  const auto& display_result = info->display_result;
  if (display_result)
    DisplayDisplayInfo(display_result);

  const auto& input_result = info->input_result;
  if (input_result)
    DisplayInputInfo(input_result);

  const auto& audio_hardware_result = info->audio_hardware_result;
  if (audio_hardware_result)
    DisplayAudioHardwareInfo(audio_hardware_result);

  const auto& sensor_result = info->sensor_result;
  if (sensor_result)
    DisplaySensorInfo(sensor_result);
}

// Create a stringified list of the category names for use in help.
std::string GetCategoryHelp() {
  std::stringstream ss;
  ss << "Category or categories to probe, as comma-separated list: [";
  const char* sep = "";
  for (auto pair : kCategorySwitches) {
    ss << sep << pair.first;
    sep = ", ";
  }
  ss << "]";
  return ss.str();
}

}  // namespace

// 'telem' sub-command for cros-health-tool:
//
// Test driver for cros_healthd's telemetry collection. Supports requesting a
// comma-separate list of categories and/or a single process at a time.
int telem_main(int argc, char** argv) {
  std::string category_help = GetCategoryHelp();
  DEFINE_string(category, "", category_help.c_str());
  DEFINE_uint32(process, 0, "Process ID to probe.");
  brillo::FlagHelper::Init(argc, argv, "telem - Device telemetry tool.");
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  base::AtExitManager at_exit_manager;

  std::map<std::string, mojom::ProbeCategoryEnum> switch_to_category(
      std::begin(kCategorySwitches), std::end(kCategorySwitches));

  logging::InitLogging(logging::LoggingSettings());

  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);

  std::unique_ptr<CrosHealthdMojoAdapter> adapter =
      CrosHealthdMojoAdapter::Create();

  // Make sure at least one flag is specified.
  if (FLAGS_category == "" && FLAGS_process == 0) {
    LOG(ERROR) << "No category or process specified.";
    return EXIT_FAILURE;
  }

  // Probe a process, if requested.
  if (FLAGS_process != 0) {
    DisplayProcessInfo(
        adapter->GetProcessInfo(static_cast<pid_t>(FLAGS_process)));
  }

  // Probe category info, if requested.
  if (FLAGS_category != "") {
    // Validate the category flag.
    std::vector<mojom::ProbeCategoryEnum> categories_to_probe;
    std::vector<std::string> input_categories = base::SplitString(
        FLAGS_category, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    for (const auto& category : input_categories) {
      auto iterator = switch_to_category.find(category);
      if (iterator == switch_to_category.end()) {
        LOG(ERROR) << "Invalid category: " << category;
        return EXIT_FAILURE;
      }
      categories_to_probe.push_back(iterator->second);
    }

    // Probe and display the category or categories.
    mojom::TelemetryInfoPtr result =
        adapter->GetTelemetryInfo(categories_to_probe);

    if (!result) {
      LOG(ERROR) << "Unable to probe telemetry info";
      return EXIT_FAILURE;
    }

    DisplayTelemetryInfo(result);
  }

  return EXIT_SUCCESS;
}

}  // namespace diagnostics
