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
#include <utility>
#include <vector>

#include <base/at_exit.h>
#include <base/logging.h>
#include <base/optional.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/task/single_thread_task_executor.h>
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

std::string ProcessStateToString(ProcessState state) {
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

std::string ErrorTypeToString(ErrorType type) {
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

void DisplayError(const ProbeErrorPtr& error) {
  std::cout << ErrorTypeToString(error->type) << ": " << error->msg
            << std::endl;
}

std::string GetArchitectureString(CpuArchitectureEnum architecture) {
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

std::string NetworkTypeToString(NetworkType type) {
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

std::string NetworkStateToString(NetworkState state) {
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

std::string PortalStateToString(PortalState state) {
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

void DisplayProcessInfo(const ProcessResultPtr& process_result,
                        const bool beauty) {
  if (process_result.is_null())
    return;

  if (process_result->is_error()) {
    DisplayError(process_result->get_error());
    return;
  }

  const auto& process = process_result->get_process_info();

  const std::vector<std::string> headers = {"command",
                                            "user_id",
                                            "priority",
                                            "nice",
                                            "uptime_ticks",
                                            "state",
                                            "total_memory_kib",
                                            "resident_memory_kib",
                                            "free_memory_kib",
                                            "bytes_read",
                                            "bytes_written",
                                            "read_system_calls",
                                            "write_system_calls",
                                            "physical_bytes_read",
                                            "physical_bytes_written",
                                            "cancelled_bytes_written"};

  // The int8_t fields need to be cast to a larger int type, otherwise they will
  // be treated as chars and display garbage. Also, wrap the command in quotes,
  // because the command-line options included in the command sometimes have
  // their own commas.
  const std::vector<std::vector<std::string>> values = {
      {"\"" + static_cast<std::string>(process->command) + "\"",
       std::to_string(process->user_id),
       std::to_string(static_cast<int>(process->priority)),
       std::to_string(static_cast<int>(process->nice)),
       std::to_string(process->uptime_ticks),
       static_cast<std::string>(ProcessStateToString(process->state)),
       std::to_string(process->total_memory_kib),
       std::to_string(process->resident_memory_kib),
       std::to_string(process->free_memory_kib),
       std::to_string(process->bytes_read),
       std::to_string(process->bytes_written),
       std::to_string(process->read_system_calls),
       std::to_string(process->write_system_calls),
       std::to_string(process->physical_bytes_read),
       std::to_string(process->physical_bytes_written),
       std::to_string(process->cancelled_bytes_written)}};

  OutputData(headers, values, beauty);
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

void DisplayAudioInfo(const AudioResultPtr& audio_result, const bool beauty) {
  if (audio_result->is_error()) {
    DisplayError(audio_result->get_error());
    return;
  }

  const auto& audio = audio_result->get_audio_info();
  if (audio.is_null()) {
    std::cout << "Device does not have audio info" << std::endl;
    return;
  }

  const std::vector<std::string> headers = {
      "output_mute",   "input_mute",        "output_device_name",
      "output_volume", "input_device_name", "input_gain",
      "underruns",     "severe_underruns"};
  const std::vector<std::vector<std::string>> values = {
      {std::to_string(audio->output_mute), std::to_string(audio->input_mute),
       audio->output_device_name, std::to_string(audio->output_volume),
       audio->input_device_name, std::to_string(audio->input_gain),
       std::to_string(audio->underruns),
       std::to_string(audio->severe_underruns)}};

  OutputData(headers, values, beauty);
}

void DisplayBootPerformanceInfo(
    const BootPerformanceResultPtr& boot_performance_result,
    const bool beauty) {
  if (boot_performance_result->is_error()) {
    DisplayError(boot_performance_result->get_error());
    return;
  }

  const auto& boot_performance =
      boot_performance_result->get_boot_performance_info();
  if (boot_performance.is_null()) {
    std::cout << "Device does not have boot_performance info" << std::endl;
    return;
  }

  const std::vector<std::string> headers = {"boot_up_seconds",
                                            "boot_up_timestamp"};
  const std::vector<std::vector<std::string>> values = {
      {std::to_string(boot_performance->boot_up_seconds),
       std::to_string(boot_performance->boot_up_timestamp)}};

  OutputData(headers, values, beauty);
}

void DisplayBlockDeviceInfo(
    const NonRemovableBlockDeviceResultPtr& block_device_result,
    const bool beauty) {
  if (block_device_result->is_error()) {
    DisplayError(block_device_result->get_error());
    return;
  }

  const std::vector<std::string> headers = {
      "path",
      "size",
      "type",
      "manfid",
      "name",
      "serial",
      "bytes_read_since_last_boot",
      "bytes_written_since_last_boot",
      "read_time_seconds_since_last_boot",
      "write_time_seconds_since_last_boot",
      "io_time_seconds_since_last_boot",
      "discard_time_seconds_since_last_boot"};

  const auto& block_devices = block_device_result->get_block_device_info();
  std::vector<std::vector<std::string>> values;
  for (const auto& device : block_devices) {
    std::string discard_time =
        !device->discard_time_seconds_since_last_boot.is_null()
            ? std::to_string(
                  device->discard_time_seconds_since_last_boot->value)
            : kNotApplicableString;
    values.push_back(
        {device->path, std::to_string(device->size), device->type,
         std::to_string(device->manufacturer_id), device->name,
         std::to_string(device->serial),
         std::to_string(device->bytes_read_since_last_boot),
         std::to_string(device->bytes_written_since_last_boot),
         std::to_string(device->read_time_seconds_since_last_boot),
         std::to_string(device->write_time_seconds_since_last_boot),
         std::to_string(device->io_time_seconds_since_last_boot),
         discard_time});
  }

  OutputData(headers, values, beauty);
}

void DisplayBluetoothInfo(const BluetoothResultPtr& bluetooth_result,
                          const bool beauty) {
  if (bluetooth_result->is_error()) {
    DisplayError(bluetooth_result->get_error());
    return;
  }

  const std::vector<std::string> headers = {"name", "address", "powered",
                                            "num_connected_devices"};

  const auto& adapters = bluetooth_result->get_bluetooth_adapter_info();
  std::vector<std::vector<std::string>> values;
  for (const auto& adapter : adapters) {
    values.push_back({adapter->name, adapter->address,
                      (adapter->powered ? "true" : "false"),
                      std::to_string(adapter->num_connected_devices)});
  }

  OutputData(headers, values, beauty);
}

void DisplayCpuInfo(const CpuResultPtr& cpu_result) {
  if (cpu_result->is_error()) {
    DisplayError(cpu_result->get_error());
    return;
  }

  // An example CpuInfo output containing a single physical CPU, which in turn
  // contains two logical CPUs, would look like the following:
  //
  // num_total_threads,architecture
  // some_uint32,some_string
  // Physical CPU:
  //     model_name
  //     some_string
  //     Logical CPU:
  //         max_clock_speed_khz,scaling_max_frequency_khz,... (six keys total)
  //         some_uint32,... (six values total)
  //         C-states:
  //             name,time_in_state_since_last_boot_us
  //             some_string,some_uint_64
  //             ... (repeated per C-state)
  //             some_string,some_uint_64
  //     Logical CPU:
  //         max_clock_speed_khz,scaling_max_frequency_khz,... (six keys total)
  //         some_uint32,... (six values total)
  //         C-states:
  //             name,time_in_state_since_last_boot_us
  //             some_string,some_uint_64
  //             ... (repeated per C-state)
  //             some_string,some_uint_64
  // Temperature Channels:
  // label, temperature_celsius
  // some_label, some_int32_t
  // some_other_label, some_other_int32_t
  //
  // Any additional physical CPUs would repeat, similarly to the two logical
  // CPUs in the example.
  const auto& cpu_info = cpu_result->get_cpu_info();
  std::cout << "num_total_threads,architecture" << std::endl;
  std::cout << cpu_info->num_total_threads << ","
            << GetArchitectureString(cpu_info->architecture) << std::endl;
  for (const auto& physical_cpu : cpu_info->physical_cpus) {
    std::cout << "Physical CPU:" << std::endl;
    std::cout << "\tmodel_name" << std::endl;
    // Remove commas from the model name before printing CSVs.
    std::string csv_model_name;
    base::RemoveChars(physical_cpu->model_name.value_or(kNotApplicableString),
                      ",", &csv_model_name);
    std::cout << "\t" << csv_model_name << std::endl;

    for (const auto& logical_cpu : physical_cpu->logical_cpus) {
      std::cout << "\tLogical CPU:" << std::endl;
      std::cout << "\t\tmax_clock_speed_khz,scaling_max_frequency_khz,scaling_"
                   "current_frequency_khz,user_time_user_hz,system_time_user_"
                   "hz,idle_time_user_hz"
                << std::endl;
      std::cout << "\t\t" << logical_cpu->max_clock_speed_khz << ","
                << logical_cpu->scaling_max_frequency_khz << ","
                << logical_cpu->scaling_current_frequency_khz << ","
                << logical_cpu->user_time_user_hz << ","
                << logical_cpu->system_time_user_hz << ","
                << logical_cpu->idle_time_user_hz << std::endl;

      std::cout << "\t\tC-states:" << std::endl;
      std::cout << "\t\t\tname,time_in_state_since_last_boot_us" << std::endl;
      for (const auto& c_state : logical_cpu->c_states) {
        std::cout << "\t\t\t" << c_state->name << ","
                  << c_state->time_in_state_since_last_boot_us << std::endl;
      }
    }
  }
  std::cout << "Temperature Channels:" << std::endl;
  std::cout << "label,temperature_celsius" << std::endl;
  for (const auto& channel : cpu_info->temperature_channels) {
    std::cout << channel->label.value_or(kNotApplicableString) << ","
              << channel->temperature_celsius << std::endl;
  }
}

void DisplayFanInfo(const FanResultPtr& fan_result, const bool beauty) {
  if (fan_result->is_error()) {
    DisplayError(fan_result->get_error());
    return;
  }

  const std::vector<std::string> headers = {"speed_rpm"};

  const auto& fans = fan_result->get_fan_info();
  std::vector<std::vector<std::string>> values;
  for (const auto& fan : fans) {
    values.push_back({std::to_string(fan->speed_rpm)});
  }

  OutputData(headers, values, beauty);
}

void DisplayNetworkInfo(const NetworkResultPtr& network_result,
                        const bool beauty) {
  if (network_result->is_error()) {
    DisplayError(network_result->get_error());
    return;
  }

  const auto& network_health = network_result->get_network_health();
  const std::vector<std::string> headers = {
      "type",        "state",        "portal_state",
      "guid",        "name",         "signal_strength",
      "mac_address", "ipv4_address", "ipv6_addresses"};

  std::vector<std::vector<std::string>> values;
  for (const auto& network : network_health->networks) {
    auto signal_strength = network->signal_strength
                               ? std::to_string(network->signal_strength->value)
                               : kNotApplicableString;
    values.push_back({NetworkTypeToString(network->type),
                      NetworkStateToString(network->state),
                      PortalStateToString(network->portal_state),
                      network->guid.value_or(kNotApplicableString),
                      network->name.value_or(kNotApplicableString),
                      signal_strength,
                      network->mac_address.value_or(kNotApplicableString),
                      network->ipv4_address.value_or(kNotApplicableString),
                      (network->ipv6_addresses.size()
                           ? base::JoinString(network->ipv6_addresses, ":")
                           : kNotApplicableString)});
  }

  OutputData(headers, values, beauty);
}

void DisplayTimezoneInfo(const TimezoneResultPtr& timezone_result,
                         const bool beauty) {
  if (timezone_result->is_error()) {
    DisplayError(timezone_result->get_error());
    return;
  }

  const auto& timezone = timezone_result->get_timezone_info();
  // Replace commas in POSIX timezone before printing CSVs.
  std::string csv_posix_timezone;
  base::ReplaceChars(timezone->posix, ",", " ", &csv_posix_timezone);

  const std::vector<std::string> headers = {"posix_timezone",
                                            "timezone_region"};
  const std::vector<std::vector<std::string>> values = {
      {csv_posix_timezone, timezone->region}};

  OutputData(headers, values, beauty);
}

void DisplayMemoryInfo(const MemoryResultPtr& memory_result,
                       const bool beauty) {
  if (memory_result->is_error()) {
    DisplayError(memory_result->get_error());
    return;
  }

  const auto& memory = memory_result->get_memory_info();
  const std::vector<std::string> headers = {
      "total_memory_kib", "free_memory_kib", "available_memory_kib",
      "page_faults_since_last_boot"};
  const std::vector<std::vector<std::string>> values = {
      {std::to_string(memory->total_memory_kib),
       std::to_string(memory->free_memory_kib),
       std::to_string(memory->available_memory_kib),
       std::to_string(memory->page_faults_since_last_boot)}};

  OutputData(headers, values, beauty);
}

void DisplayBacklightInfo(const BacklightResultPtr& backlight_result,
                          const bool beauty) {
  if (backlight_result->is_error()) {
    DisplayError(backlight_result->get_error());
    return;
  }

  const std::vector<std::string> headers = {"path", "max_brightness",
                                            "brightness"};

  const auto& backlights = backlight_result->get_backlight_info();
  std::vector<std::vector<std::string>> values;
  for (const auto& backlight : backlights) {
    values.push_back({backlight->path,
                      std::to_string(backlight->max_brightness),
                      std::to_string(backlight->brightness)});
  }

  OutputData(headers, values, beauty);
}

void DisplayStatefulPartitionInfo(
    const StatefulPartitionResultPtr& stateful_partition_result,
    const bool beauty) {
  if (stateful_partition_result->is_error()) {
    DisplayError(stateful_partition_result->get_error());
    return;
  }

  const auto& stateful_partition_info =
      stateful_partition_result->get_partition_info();
  const std::vector<std::string> headers = {"available_space", "total_space",
                                            "filesystem", "mount_source"};
  const std::vector<std::vector<std::string>> values = {
      {std::to_string(stateful_partition_info->available_space),
       std::to_string(stateful_partition_info->total_space),
       stateful_partition_info->filesystem,
       stateful_partition_info->mount_source}};

  OutputData(headers, values, beauty);
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
    DisplayBlockDeviceInfo(block_device_result, beauty);

  const auto& cpu_result = info->cpu_result;
  if (cpu_result)
    DisplayCpuInfo(cpu_result);

  const auto& timezone_result = info->timezone_result;
  if (timezone_result)
    DisplayTimezoneInfo(timezone_result, beauty);

  const auto& memory_result = info->memory_result;
  if (memory_result)
    DisplayMemoryInfo(memory_result, beauty);

  const auto& backlight_result = info->backlight_result;
  if (backlight_result)
    DisplayBacklightInfo(backlight_result, beauty);

  const auto& fan_result = info->fan_result;
  if (fan_result)
    DisplayFanInfo(fan_result, beauty);

  const auto& stateful_partition_result = info->stateful_partition_result;
  if (stateful_partition_result)
    DisplayStatefulPartitionInfo(stateful_partition_result, beauty);

  const auto& bluetooth_result = info->bluetooth_result;
  if (bluetooth_result)
    DisplayBluetoothInfo(bluetooth_result, beauty);

  const auto& system_result = info->system_result;
  if (system_result)
    DisplaySystemInfo(system_result, beauty);

  const auto& network_result = info->network_result;
  if (network_result)
    DisplayNetworkInfo(network_result, beauty);

  const auto& audio_result = info->audio_result;
  if (audio_result)
    DisplayAudioInfo(audio_result, beauty);

  const auto& boot_performance_result = info->boot_performance_result;
  if (boot_performance_result)
    DisplayBootPerformanceInfo(boot_performance_result, beauty);
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
        adapter->GetProcessInfo(static_cast<pid_t>(FLAGS_process)),
        FLAGS_beauty);
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
