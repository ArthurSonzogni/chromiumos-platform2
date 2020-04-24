// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <utility>

#include <base/at_exit.h>
#include <base/logging.h>
#include <base/message_loop/message_loop.h>
#include <base/optional.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_util.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include "diagnostics/cros_healthd_mojo_adapter/cros_healthd_mojo_adapter.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace {

using chromeos::cros_healthd::mojom::CpuArchitectureEnum;

constexpr std::pair<const char*,
                    chromeos::cros_healthd::mojom::ProbeCategoryEnum>
    kCategorySwitches[] = {
        {"battery", chromeos::cros_healthd::mojom::ProbeCategoryEnum::kBattery},
        {"storage", chromeos::cros_healthd::mojom::ProbeCategoryEnum::
                        kNonRemovableBlockDevices},
        {"cached_vpd",
         chromeos::cros_healthd::mojom::ProbeCategoryEnum::kCachedVpdData},
        {"cpu", chromeos::cros_healthd::mojom::ProbeCategoryEnum::kCpu},
        {"timezone",
         chromeos::cros_healthd::mojom::ProbeCategoryEnum::kTimezone},
        {"memory", chromeos::cros_healthd::mojom::ProbeCategoryEnum::kMemory},
        {"backlight",
         chromeos::cros_healthd::mojom::ProbeCategoryEnum::kBacklight},
        {"fan", chromeos::cros_healthd::mojom::ProbeCategoryEnum::kFan},
};

std::string ErrorTypeToString(chromeos::cros_healthd::mojom::ErrorType type) {
  switch (type) {
    case chromeos::cros_healthd::mojom::ErrorType::kFileReadError:
      return "File Read Error";
    case chromeos::cros_healthd::mojom::ErrorType::kParseError:
      return "Parse Error";
    case chromeos::cros_healthd::mojom::ErrorType::kSystemUtilityError:
      return "Error running system utility";
  }
}

void DisplayError(const chromeos::cros_healthd::mojom::ProbeErrorPtr& error) {
  printf("%s: %s", ErrorTypeToString(error->type).c_str(), error->msg.c_str());
}

std::string GetArchitectureString(CpuArchitectureEnum architecture) {
  switch (architecture) {
    case CpuArchitectureEnum::kUnknown:
      return "unknown";
    case CpuArchitectureEnum::kX86_64:
      return "x86_64";
  }
}

void DisplayBatteryInfo(
    const chromeos::cros_healthd::mojom::BatteryResultPtr& battery_result) {
  if (battery_result->is_error()) {
    DisplayError(battery_result->get_error());
    return;
  }

  const auto& battery = battery_result->get_battery_info();
  if (battery.is_null()) {
    printf("Device does not have battery\n");
    return;
  }

  printf(
      "charge_full,charge_full_design,cycle_count,serial_number,"
      "vendor(manufacturer),voltage_now,voltage_min_design,"
      "manufacture_date_smart,temperature_smart,model_name,charge_now,"
      "current_now,technology,status\n");

  std::string manufacture_date_smart = battery->manufacture_date.value_or("NA");
  std::string temperature_smart =
      !battery->temperature.is_null()
          ? std::to_string(battery->temperature->value)
          : "NA";

  printf("%f,%f,%ld,%s,%s,%f,%f,%s,%s,%s,%f,%f,%s,%s\n", battery->charge_full,
         battery->charge_full_design, battery->cycle_count,
         battery->serial_number.c_str(), battery->vendor.c_str(),
         battery->voltage_now, battery->voltage_min_design,
         manufacture_date_smart.c_str(), temperature_smart.c_str(),
         battery->model_name.c_str(), battery->charge_now, battery->current_now,
         battery->technology.c_str(), battery->status.c_str());
}

void DisplayBlockDeviceInfo(
    const chromeos::cros_healthd::mojom::NonRemovableBlockDeviceResultPtr&
        block_device_result) {
  if (block_device_result->is_error()) {
    DisplayError(block_device_result->get_error());
    return;
  }

  const auto& block_devices = block_device_result->get_block_device_info();
  printf("path,size,type,manfid,name,serial\n");
  for (const auto& device : block_devices) {
    printf("%s,%ld,%s,0x%x,%s,0x%x\n", device->path.c_str(), device->size,
           device->type.c_str(), static_cast<int>(device->manufacturer_id),
           device->name.c_str(), device->serial);
  }
}

void DisplayCachedVpdInfo(
    const chromeos::cros_healthd::mojom::CachedVpdResultPtr& vpd_result) {
  if (vpd_result->is_error()) {
    DisplayError(vpd_result->get_error());
    return;
  }

  const auto& vpd = vpd_result->get_vpd_info();
  printf("sku_number\n");
  if (vpd->sku_number.has_value())
    printf("%s\n", vpd->sku_number.value().c_str());
  else
    printf("NA\n");
}

void DisplayCpuInfo(
    const chromeos::cros_healthd::mojom::CpuResultPtr& cpu_result) {
  if (cpu_result->is_error()) {
    DisplayError(cpu_result->get_error());
    return;
  }

  const auto& cpus = cpu_result->get_cpu_info();
  printf("model_name,architecture,max_clock_speed_khz\n");
  for (const auto& cpu : cpus) {
    // Remove commas from the model name before printing CSVs.
    std::string csv_model_name;
    base::RemoveChars(cpu->model_name, ",", &csv_model_name);
    printf("%s,%s,%u\n", csv_model_name.c_str(),
           GetArchitectureString(cpu->architecture).c_str(),
           cpu->max_clock_speed_khz);
  }
}

void DisplayFanInfo(
    const chromeos::cros_healthd::mojom::FanResultPtr& fan_result) {
  if (fan_result->is_error()) {
    DisplayError(fan_result->get_error());
    return;
  }

  const auto& fans = fan_result->get_fan_info();
  printf("speed_rpm\n");
  for (const auto& fan : fans) {
    printf("%u\n", fan->speed_rpm);
  }
}

void DisplayTimezoneInfo(
    const chromeos::cros_healthd::mojom::TimezoneResultPtr& timezone_result) {
  if (timezone_result->is_error()) {
    DisplayError(timezone_result->get_error());
    return;
  }

  const auto& timezone = timezone_result->get_timezone_info();
  // Replace commas in POSIX timezone before printing CSVs.
  std::string csv_posix_timezone;
  base::ReplaceChars(timezone->posix, ",", " ", &csv_posix_timezone);
  printf("posix_timezone,timezone_region\n");
  printf("%s,%s\n", csv_posix_timezone.c_str(), timezone->region.c_str());
}

void DisplayMemoryInfo(
    const chromeos::cros_healthd::mojom::MemoryResultPtr& memory_result) {
  if (memory_result->is_error()) {
    DisplayError(memory_result->get_error());
    return;
  }

  const auto& memory = memory_result->get_memory_info();
  printf(
      "total_memory_kib,free_memory_kib,available_memory_kib,page_faults_since_"
      "last_boot\n");
  printf("%u,%u,%u,%u\n", memory->total_memory_kib, memory->free_memory_kib,
         memory->available_memory_kib, memory->page_faults_since_last_boot);
}

void DisplayBacklightInfo(
    const chromeos::cros_healthd::mojom::BacklightResultPtr& backlight_result) {
  if (backlight_result->is_error()) {
    DisplayError(backlight_result->get_error());
    return;
  }

  const auto& backlights = backlight_result->get_backlight_info();
  printf("path,max_brightness,brightness\n");
  for (const auto& backlight : backlights) {
    printf("%s,%u,%u\n", backlight->path.c_str(), backlight->max_brightness,
           backlight->brightness);
  }
}

// Displays the retrieved telemetry information to the console.
void DisplayTelemetryInfo(
    const chromeos::cros_healthd::mojom::TelemetryInfoPtr& info) {
  const auto& battery_result = info->battery_result;
  if (battery_result)
    DisplayBatteryInfo(battery_result);

  const auto& block_device_result = info->block_device_result;
  if (block_device_result)
    DisplayBlockDeviceInfo(block_device_result);

  const auto& vpd_result = info->vpd_result;
  if (vpd_result)
    DisplayCachedVpdInfo(vpd_result);

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
}

// Create a stringified list of the category names for use in help.
std::string GetCategoryHelp() {
  std::stringstream ss;
  ss << "Category to probe: [";
  const char* sep = "";
  for (auto pair : kCategorySwitches) {
    ss << sep << pair.first;
    sep = ", ";
  }
  ss << "]";
  return ss.str();
}

}  // namespace

// 'telem' command-line tool:
//
// Test driver for cros_healthd's telemetry collection. Supports requesting a
// single category at a time.
int main(int argc, char** argv) {
  std::string category_help = GetCategoryHelp();
  DEFINE_string(category, "", category_help.c_str());
  brillo::FlagHelper::Init(argc, argv, "telem - Device telemetry tool.");
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  base::AtExitManager at_exit_manager;

  std::map<std::string, chromeos::cros_healthd::mojom::ProbeCategoryEnum>
      switch_to_category(std::begin(kCategorySwitches),
                         std::end(kCategorySwitches));

  logging::InitLogging(logging::LoggingSettings());

  base::MessageLoopForIO message_loop;

  // Make sure at least one category is specified.
  if (FLAGS_category == "") {
    LOG(ERROR) << "No category specified.";
    return EXIT_FAILURE;
  }
  // Validate the category flag.
  auto iterator = switch_to_category.find(FLAGS_category);
  if (iterator == switch_to_category.end()) {
    LOG(ERROR) << "Invalid category: " << FLAGS_category;
    return EXIT_FAILURE;
  }

  // Probe and display the category.
  diagnostics::CrosHealthdMojoAdapter adapter;
  const std::vector<chromeos::cros_healthd::mojom::ProbeCategoryEnum>
      categories_to_probe = {iterator->second};
  DisplayTelemetryInfo(adapter.GetTelemetryInfo(categories_to_probe));

  return EXIT_SUCCESS;
}
