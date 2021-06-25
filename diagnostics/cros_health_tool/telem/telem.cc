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
using chromeos::cros_healthd::mojom::BootPerformanceResultPtr;
using chromeos::cros_healthd::mojom::CpuArchitectureEnum;
using chromeos::cros_healthd::mojom::CpuResultPtr;
using chromeos::cros_healthd::mojom::ErrorType;
using chromeos::cros_healthd::mojom::FanResultPtr;
using chromeos::cros_healthd::mojom::MemoryResultPtr;
using chromeos::cros_healthd::mojom::NetworkResultPtr;
using chromeos::cros_healthd::mojom::NonRemovableBlockDeviceResultPtr;
using chromeos::cros_healthd::mojom::NullableUint64Ptr;
using chromeos::cros_healthd::mojom::ProbeCategoryEnum;
using chromeos::cros_healthd::mojom::ProbeErrorPtr;
using chromeos::cros_healthd::mojom::ProcessResultPtr;
using chromeos::cros_healthd::mojom::ProcessState;
using chromeos::cros_healthd::mojom::StatefulPartitionResultPtr;
using chromeos::cros_healthd::mojom::SystemResultPtr;
using chromeos::cros_healthd::mojom::TelemetryInfoPtr;
using chromeos::cros_healthd::mojom::TimezoneResultPtr;
using chromeos::network_config::mojom::NetworkType;
using chromeos::network_config::mojom::PortalState;
using chromeos::network_health::mojom::NetworkState;
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
    {"network", ProbeCategoryEnum::kNetwork},
    {"audio", ProbeCategoryEnum::kAudio},
    {"boot_performance", ProbeCategoryEnum::kBootPerformance},
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

void OutputTableLine(const std::string& header,
                     const std::string& value,
                     const size_t max_len_header) {
  std::cout << header << std::string(max_len_header - header.length(), ' ')
            << " : " << value << std::endl;
}

void OutputTable(const std::vector<std::string>& headers,
                 const std::vector<std::vector<std::string>>& values) {
  size_t max_len_header = 0;
  for (const auto& header : headers) {
    max_len_header = std::max(max_len_header, header.length());
  }

  for (const auto& value : values) {
    for (auto i = 0; i < headers.size(); i++) {
      OutputTableLine(headers[i], value[i], max_len_header);
    }
    std::cout << std::endl;
  }
}

void OutputData(const std::vector<std::string>& headers,
                const std::vector<std::vector<std::string>>& values,
                const bool beauty) {
  if (!beauty) {
    OutputCSV(headers, values);
  } else {
    OutputTable(headers, values);
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

void DisplayBatteryInfo(const BatteryResultPtr& battery_result,
                        const bool beauty) {
  if (battery_result->is_error()) {
    DisplayError(battery_result->get_error());
    return;
  }

  const auto& battery = battery_result->get_battery_info();
  if (battery.is_null()) {
    std::cout << "Device does not have battery" << std::endl;
    return;
  }

  const std::vector<std::string> headers = {
      "charge_full",          "charge_full_design",
      "cycle_count",          "serial_number",
      "vendor(manufacturer)", "voltage_now",
      "voltage_min_design",   "manufacture_date_smart",
      "temperature_smart",    "model_name",
      "charge_now",           "current_now",
      "technology",           "status"};

  std::string manufacture_date_smart =
      battery->manufacture_date.value_or(kNotApplicableString);
  std::string temperature_smart =
      !battery->temperature.is_null()
          ? std::to_string(battery->temperature->value)
          : kNotApplicableString;

  const std::vector<std::vector<std::string>> values = {
      {std::to_string(battery->charge_full),
       std::to_string(battery->charge_full_design),
       std::to_string(battery->cycle_count), battery->serial_number,
       battery->vendor, std::to_string(battery->voltage_now),
       std::to_string(battery->voltage_min_design), manufacture_date_smart,
       temperature_smart, battery->model_name,
       std::to_string(battery->charge_now),
       std::to_string(battery->current_now), battery->technology,
       battery->status}};

  OutputData(headers, values, beauty);
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

void DisplaySystemInfo(const SystemResultPtr& system_result,
                       const bool beauty) {
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

  OutputData(headers, values, beauty);
}

// Displays the retrieved telemetry information to the console.
void DisplayTelemetryInfo(const TelemetryInfoPtr& info, const bool beauty) {
  const auto& battery_result = info->battery_result;
  if (battery_result)
    DisplayBatteryInfo(battery_result, beauty);

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
    DisplaySystemInfo(system_result, beauty);

  const auto& network_result = info->network_result;
  if (network_result)
    DisplayNetworkInfo(network_result);

  const auto& audio_result = info->audio_result;
  if (audio_result)
    DisplayAudioInfo(audio_result);

  const auto& boot_performance_result = info->boot_performance_result;
  if (boot_performance_result)
    DisplayBootPerformanceInfo(boot_performance_result);
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
  DEFINE_bool(beauty, false, "Display info with beautiful format.");
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

    DisplayTelemetryInfo(result, FLAGS_beauty);
  }

  return EXIT_SUCCESS;
}

}  // namespace diagnostics
