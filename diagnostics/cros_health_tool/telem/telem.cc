// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_health_tool/telem/telem.h"

#include <sys/types.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <base/at_exit.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/optional.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/task/single_thread_task_executor.h>
#include <base/values.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include "diagnostics/cros_healthd_mojo_adapter/cros_healthd_mojo_adapter.h"
#include "mojo/cros_healthd_probe.mojom.h"
#include "mojo/network_health.mojom.h"
#include "mojo/network_types.mojom.h"

namespace diagnostics {

namespace {

using chromeos::cros_healthd::mojom::AudioResultPtr;
using chromeos::cros_healthd::mojom::BacklightResultPtr;
using chromeos::cros_healthd::mojom::BatteryResultPtr;
using chromeos::cros_healthd::mojom::BluetoothResultPtr;
using chromeos::cros_healthd::mojom::BootMode;
using chromeos::cros_healthd::mojom::BootPerformanceResultPtr;
using chromeos::cros_healthd::mojom::BusDeviceClass;
using chromeos::cros_healthd::mojom::BusInfo;
using chromeos::cros_healthd::mojom::BusResultPtr;
using chromeos::cros_healthd::mojom::CpuArchitectureEnum;
using chromeos::cros_healthd::mojom::CpuResultPtr;
using chromeos::cros_healthd::mojom::ErrorType;
using chromeos::cros_healthd::mojom::FanResultPtr;
using chromeos::cros_healthd::mojom::GraphicsResultPtr;
using chromeos::cros_healthd::mojom::MemoryResultPtr;
using chromeos::cros_healthd::mojom::NetworkResultPtr;
using chromeos::cros_healthd::mojom::NonRemovableBlockDeviceResultPtr;
using chromeos::cros_healthd::mojom::NullableUint64Ptr;
using chromeos::cros_healthd::mojom::OsInfoPtr;
using chromeos::cros_healthd::mojom::ProbeCategoryEnum;
using chromeos::cros_healthd::mojom::ProbeErrorPtr;
using chromeos::cros_healthd::mojom::ProcessResultPtr;
using chromeos::cros_healthd::mojom::ProcessState;
using chromeos::cros_healthd::mojom::StatefulPartitionResultPtr;
using chromeos::cros_healthd::mojom::SystemResultPtr;
using chromeos::cros_healthd::mojom::SystemResultV2Ptr;
using chromeos::cros_healthd::mojom::TelemetryInfoPtr;
using chromeos::cros_healthd::mojom::TimezoneResultPtr;
using chromeos::cros_healthd::mojom::TpmGSCVersion;
using chromeos::cros_healthd::mojom::TpmResultPtr;
using chromeos::network_config::mojom::NetworkType;
using chromeos::network_config::mojom::PortalState;
using chromeos::network_health::mojom::NetworkState;
using chromeos::network_health::mojom::SignalStrengthStatsPtr;
using chromeos::network_health::mojom::UInt32ValuePtr;

// Value printed for optional fields when they aren't populated.
constexpr char kNotApplicableString[] = "N/A";

constexpr std::pair<const char*, ProbeCategoryEnum> kCategorySwitches[] = {
    {"battery", ProbeCategoryEnum::kBattery},
    {"storage", ProbeCategoryEnum::kNonRemovableBlockDevices},
    {"cpu", ProbeCategoryEnum::kCpu},
    {"timezone", ProbeCategoryEnum::kTimezone},
    {"memory", ProbeCategoryEnum::kMemory},
    {"backlight", ProbeCategoryEnum::kBacklight},
    {"fan", ProbeCategoryEnum::kFan},
    {"stateful_partition", ProbeCategoryEnum::kStatefulPartition},
    {"bluetooth", ProbeCategoryEnum::kBluetooth},
    {"system", ProbeCategoryEnum::kSystem},
    {"system2", ProbeCategoryEnum::kSystem2},
    {"network", ProbeCategoryEnum::kNetwork},
    {"audio", ProbeCategoryEnum::kAudio},
    {"boot_performance", ProbeCategoryEnum::kBootPerformance},
    {"bus", ProbeCategoryEnum::kBus},
    {"tpm", ProbeCategoryEnum::kTpm},
    {"graphics", ProbeCategoryEnum::kGraphics},
};

std::string EnumToString(ProcessState state) {
  switch (state) {
    case ProcessState::kRunning:
      return "Running";
    case ProcessState::kSleeping:
      return "Sleeping";
    case ProcessState::kWaiting:
      return "Waiting";
    case ProcessState::kZombie:
      return "Zombie";
    case ProcessState::kStopped:
      return "Stopped";
    case ProcessState::kTracingStop:
      return "Tracing Stop";
    case ProcessState::kDead:
      return "Dead";
  }
}

std::string EnumToString(ErrorType type) {
  switch (type) {
    case ErrorType::kUnknown:
      return "Unknown Error";
    case ErrorType::kFileReadError:
      return "File Read Error";
    case ErrorType::kParseError:
      return "Parse Error";
    case ErrorType::kSystemUtilityError:
      return "Error running system utility";
    case ErrorType::kServiceUnavailable:
      return "External service not aviailable";
  }
}

std::string EnumToString(CpuArchitectureEnum architecture) {
  switch (architecture) {
    case CpuArchitectureEnum::kUnknown:
      return "unknown";
    case CpuArchitectureEnum::kX86_64:
      return "x86_64";
    case CpuArchitectureEnum::kAArch64:
      return "aarch64";
    case CpuArchitectureEnum::kArmv7l:
      return "armv7l";
  }
}

std::string EnumToString(NetworkType type) {
  switch (type) {
    case NetworkType::kAll:
      return "Unknown";
    case NetworkType::kCellular:
      return "Cellular";
    case NetworkType::kEthernet:
      return "Ethernet";
    case NetworkType::kMobile:
      return "Mobile";
    case NetworkType::kTether:
      return "Tether";
    case NetworkType::kVPN:
      return "VPN";
    case NetworkType::kWireless:
      return "Wireless";
    case NetworkType::kWiFi:
      return "WiFi";
  }
}

std::string EnumToString(NetworkState state) {
  switch (state) {
    case NetworkState::kUninitialized:
      return "Uninitialized";
    case NetworkState::kDisabled:
      return "Disabled";
    case NetworkState::kProhibited:
      return "Prohibited";
    case NetworkState::kNotConnected:
      return "Not Connected";
    case NetworkState::kConnecting:
      return "Connecting";
    case NetworkState::kPortal:
      return "Portal";
    case NetworkState::kConnected:
      return "Connected";
    case NetworkState::kOnline:
      return "Online";
  }
}

std::string EnumToString(PortalState state) {
  switch (state) {
    case PortalState::kUnknown:
      return "Unknown";
    case PortalState::kOnline:
      return "Online";
    case PortalState::kPortalSuspected:
      return "Portal Suspected";
    case PortalState::kPortal:
      return "Portal Detected";
    case PortalState::kProxyAuthRequired:
      return "Proxy Auth Required";
    case PortalState::kNoInternet:
      return "No Internet";
  }
}

std::string EnumToString(BusDeviceClass device_class) {
  switch (device_class) {
    case BusDeviceClass::kOthers:
      return "others";
    case BusDeviceClass::kDisplayController:
      return "display controller";
    case BusDeviceClass::kEthernetController:
      return "ethernet controller";
    case BusDeviceClass::kWirelessController:
      return "wireless controller";
    case BusDeviceClass::kBluetoothAdapter:
      return "bluetooth controller";
  }
}

std::string EnumToString(BootMode mode) {
  switch (mode) {
    case BootMode::kUnknown:
      return "Unknown";
    case BootMode::kCrosSecure:
      return "cros_secure";
    case BootMode::kCrosEfi:
      return "cros_efi";
    case BootMode::kCrosLegacy:
      return "cros_legacy";
    case BootMode::kCrosEfiSecure:
      return "cros_efi_secure";
  }
}

std::string EnumToString(TpmGSCVersion version) {
  switch (version) {
    case TpmGSCVersion::kNotGSC:
      return "NotGSC";
    case TpmGSCVersion::kCr50:
      return "Cr50";
    case TpmGSCVersion::kTi50:
      return "Ti50";
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
  } else if constexpr (std::is_same_v<T, base::Optional<std::string>>) {
    if (value.has_value())
      SetJsonDictValue(key, value.value(), output);
  } else if constexpr (std::is_same_v<T, NullableUint64Ptr>) {
    if (value)
      SetJsonDictValue(key, value->value, output);
  } else if constexpr (std::is_same_v<T, UInt32ValuePtr>) {
    if (value)
      SetJsonDictValue(key, value->value, output);
  } else if constexpr (std::is_enum_v<T>) {
    SetJsonDictValue(key, EnumToString(value), output);
  } else {
    output->SetKey(key, base::Value(value));
  }
}

void OutputCSVLine(const std::vector<std::string>& datas,
                   const std::string separator = ",") {
  bool is_first = true;
  for (const auto& data : datas) {
    if (!is_first) {
      std::cout << separator;
    }
    is_first = false;
    std::cout << data;
  }
  std::cout << std::endl;
}

void OutputCSV(const std::vector<std::string>& headers,
               const std::vector<std::vector<std::string>>& values) {
  OutputCSVLine(headers);
  for (const auto& value : values) {
    OutputCSVLine(value);
  }
}

void OutputJson(const base::Value& output) {
  std::string json;
  base::JSONWriter::WriteWithOptions(
      output, base::JSONWriter::Options::OPTIONS_PRETTY_PRINT, &json);

  std::cout << json << std::endl;
}

void DisplayError(const ProbeErrorPtr& error) {
  base::Value output{base::Value::Type::DICTIONARY};
  SET_DICT(type, error, &output);
  SET_DICT(msg, error, &output);

  OutputJson(output);
}

void DisplayProcessInfo(const ProcessResultPtr& result) {
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
  SET_DICT(nice, info, &output);
  SET_DICT(physical_bytes_read, info, &output);
  SET_DICT(physical_bytes_written, info, &output);
  SET_DICT(priority, info, &output);
  SET_DICT(read_system_calls, info, &output);
  SET_DICT(resident_memory_kib, info, &output);
  SET_DICT(state, info, &output);
  SET_DICT(total_memory_kib, info, &output);
  SET_DICT(uptime_ticks, info, &output);
  SET_DICT(user_id, info, &output);
  SET_DICT(write_system_calls, info, &output);

  OutputJson(output);
}

void DisplayBatteryInfo(const BatteryResultPtr& result) {
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

void DisplayAudioInfo(const AudioResultPtr& audio_result) {
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

void DisplayBootPerformanceInfo(const BootPerformanceResultPtr& result) {
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

void DisplayBlockDeviceInfo(const NonRemovableBlockDeviceResultPtr& result) {
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

void DisplayBluetoothInfo(const BluetoothResultPtr& result) {
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

    adapters->Append(std::move(data));
  }

  OutputJson(output);
}

void DisplayCpuInfo(const CpuResultPtr& result) {
  if (result->is_error()) {
    DisplayError(result->get_error());
    return;
  }

  const auto& info = result->get_cpu_info();

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

  if (info->keylocker_info) {
    auto* out_keylocker = output.SetKey(
        "keylocker_info", base::Value{base::Value::Type::DICTIONARY});
    SET_DICT(keylocker_configured, info->keylocker_info, out_keylocker);
  }
  OutputJson(output);
}

void DisplayFanInfo(const FanResultPtr& result) {
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

void DisplayNetworkInfo(const NetworkResultPtr& result) {
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

void DisplayTimezoneInfo(const TimezoneResultPtr& result) {
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

void DisplayMemoryInfo(const MemoryResultPtr& result) {
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

  OutputJson(output);
}

void DisplayBacklightInfo(const BacklightResultPtr& result) {
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

void DisplayStatefulPartitionInfo(const StatefulPartitionResultPtr& result) {
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

void DisplaySystemInfo(const SystemResultPtr& system_result) {
  if (system_result->is_error()) {
    DisplayError(system_result->get_error());
    return;
  }
  const auto& system_info = system_result->get_system_info();
  const std::vector<std::string> headers = {
      "first_power_date",   "manufacture_date",
      "product_sku_number", "product_serial_number",
      "marketing_name",     "bios_version",
      "board_name",         "board_version",
      "chassis_type",       "product_name",
      "os_version",         "os_channel"};
  std::string chassis_type =
      !system_info->chassis_type.is_null()
          ? std::to_string(system_info->chassis_type->value)
          : kNotApplicableString;
  std::string os_version =
      base::JoinString({system_info->os_version->release_milestone,
                        system_info->os_version->build_number,
                        system_info->os_version->patch_number},
                       ".");

  // The marketing name sometimes has a comma, for example:
  // "Acer Chromebook Spin 11 (CP311-H1, CP311-1HN)"
  // This messes up the tast logic, which splits on commas. To fix it, we
  // replace any ", " patterns found with "/".
  std::string marketing_name = system_info->marketing_name;
  base::ReplaceSubstringsAfterOffset(&marketing_name, 0, ", ", "/");

  const std::vector<std::vector<std::string>> values = {
      {system_info->first_power_date.value_or(kNotApplicableString),
       system_info->manufacture_date.value_or(kNotApplicableString),
       system_info->product_sku_number.value_or(kNotApplicableString),
       system_info->product_serial_number.value_or(kNotApplicableString),
       marketing_name, system_info->bios_version.value_or(kNotApplicableString),
       system_info->board_name.value_or(kNotApplicableString),
       system_info->board_version.value_or(kNotApplicableString), chassis_type,
       system_info->product_name.value_or(kNotApplicableString), os_version,
       system_info->os_version->release_channel}};

  OutputCSV(headers, values);
}

void DisplaySystemInfoV2(const SystemResultV2Ptr& system_result) {
  if (system_result->is_error()) {
    DisplayError(system_result->get_error());
    return;
  }
  const auto& system_info = system_result->get_system_info_v2();
  base::Value output{base::Value::Type::DICTIONARY};

  const auto& os_info = system_info->os_info;
  auto* out_os_info =
      output.SetKey("os_info", base::Value{base::Value::Type::DICTIONARY});
  SET_DICT(code_name, os_info, out_os_info);
  SET_DICT(marketing_name, os_info, out_os_info);
  SET_DICT(boot_mode, os_info, out_os_info);

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

void DisplayBusDevices(const BusResultPtr& bus_result) {
  if (bus_result->is_error()) {
    DisplayError(bus_result->get_error());
    return;
  }

  const auto& devices = bus_result->get_bus_devices();

  base::Value output{base::Value::Type::DICTIONARY};
  auto* out_devices =
      output.SetKey("devices", base::Value{base::Value::Type::LIST});
  for (const auto& device : devices) {
    base::Value out_device{base::Value::Type::DICTIONARY};
    SET_DICT(vendor_name, device, &out_device);
    SET_DICT(product_name, device, &out_device);
    SET_DICT(device_class, device, &out_device);
    auto* out_bus_info = out_device.SetKey(
        "bus_info", base::Value{base::Value::Type::DICTIONARY});
    switch (device->bus_info->which()) {
      case BusInfo::Tag::PCI_BUS_INFO: {
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
      case BusInfo::Tag::USB_BUS_INFO: {
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
        break;
      }
    }
    out_devices->Append(std::move(out_device));
  }

  OutputJson(output);
}

void DisplayTpmInfo(const TpmResultPtr& result) {
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

void DisplayGraphicsInfo(const GraphicsResultPtr& graphics_result) {
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

  auto* gles_extensions =
      out_gles_info->SetKey("extensions", base::Value{base::Value::Type::LIST});
  for (const auto& ext : gles_info->extensions) {
    gles_extensions->Append(ext);
  }

  const auto& egl_info = info->egl_info;
  auto* out_egl_info =
      output.SetKey("egl_info", base::Value{base::Value::Type::DICTIONARY});
  SET_DICT(version, egl_info, out_egl_info);
  SET_DICT(vendor, egl_info, out_egl_info);
  SET_DICT(client_api, egl_info, out_egl_info);

  auto* egl_extensions =
      out_egl_info->SetKey("extensions", base::Value{base::Value::Type::LIST});
  for (const auto& ext : egl_info->extensions) {
    egl_extensions->Append(ext);
  }

  OutputJson(output);
}

// Displays the retrieved telemetry information to the console.
void DisplayTelemetryInfo(const TelemetryInfoPtr& info) {
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

  const auto& system_result = info->system_result;
  if (system_result)
    DisplaySystemInfo(system_result);

  const auto& network_result = info->network_result;
  if (network_result)
    DisplayNetworkInfo(network_result);

  const auto& audio_result = info->audio_result;
  if (audio_result)
    DisplayAudioInfo(audio_result);

  const auto& boot_performance_result = info->boot_performance_result;
  if (boot_performance_result)
    DisplayBootPerformanceInfo(boot_performance_result);

  const auto& bus_result = info->bus_result;
  if (bus_result)
    DisplayBusDevices(bus_result);

  const auto& tpm_result = info->tpm_result;
  if (tpm_result)
    DisplayTpmInfo(tpm_result);

  const auto& system_result_v2 = info->system_result_v2;
  if (system_result_v2)
    DisplaySystemInfoV2(system_result_v2);

  const auto& graphics_result = info->graphics_result;
  if (graphics_result)
    DisplayGraphicsInfo(graphics_result);
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

  std::map<std::string, chromeos::cros_healthd::mojom::ProbeCategoryEnum>
      switch_to_category(std::begin(kCategorySwitches),
                         std::end(kCategorySwitches));

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
    std::vector<chromeos::cros_healthd::mojom::ProbeCategoryEnum>
        categories_to_probe;
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
    chromeos::cros_healthd::mojom::TelemetryInfoPtr result =
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
