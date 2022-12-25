// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/metrics_utils.h"

#include <optional>
#include <set>
#include <string>

#include <base/logging.h>
#include <metrics/metrics_library.h>
#include <mojo/public/cpp/bindings/struct_ptr.h>

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

std::optional<std::string> GetMetricName(mojom::ProbeCategoryEnum category) {
  switch (category) {
    case mojom::ProbeCategoryEnum::kUnknown:
      // No metric name for the unknown category.
      return std::nullopt;
    case mojom::ProbeCategoryEnum::kBattery:
      return "ChromeOS.Healthd.TelemetryResult.Battery";
    case mojom::ProbeCategoryEnum::kCpu:
      return "ChromeOS.Healthd.TelemetryResult.Cpu";
    case mojom::ProbeCategoryEnum::kNonRemovableBlockDevices:
      return "ChromeOS.Healthd.TelemetryResult.BlockDevice";
    case mojom::ProbeCategoryEnum::kTimezone:
      return "ChromeOS.Healthd.TelemetryResult.Timezone";
    case mojom::ProbeCategoryEnum::kMemory:
      return "ChromeOS.Healthd.TelemetryResult.Memory";
    case mojom::ProbeCategoryEnum::kBacklight:
      return "ChromeOS.Healthd.TelemetryResult.Backlight";
    case mojom::ProbeCategoryEnum::kFan:
      return "ChromeOS.Healthd.TelemetryResult.Fan";
    case mojom::ProbeCategoryEnum::kStatefulPartition:
      return "ChromeOS.Healthd.TelemetryResult.StatefulPartition";
    case mojom::ProbeCategoryEnum::kBluetooth:
      return "ChromeOS.Healthd.TelemetryResult.Bluetooth";
    case mojom::ProbeCategoryEnum::kSystem:
      return "ChromeOS.Healthd.TelemetryResult.System";
    case mojom::ProbeCategoryEnum::kNetwork:
      return "ChromeOS.Healthd.TelemetryResult.Network";
    case mojom::ProbeCategoryEnum::kAudio:
      return "ChromeOS.Healthd.TelemetryResult.Audio";
    case mojom::ProbeCategoryEnum::kBootPerformance:
      return "ChromeOS.Healthd.TelemetryResult.BootPerformance";
    case mojom::ProbeCategoryEnum::kBus:
      return "ChromeOS.Healthd.TelemetryResult.Bus";
    case mojom::ProbeCategoryEnum::kTpm:
      return "ChromeOS.Healthd.TelemetryResult.Tpm";
    case mojom::ProbeCategoryEnum::kNetworkInterface:
      return "ChromeOS.Healthd.TelemetryResult.NetworkInterface";
    case mojom::ProbeCategoryEnum::kGraphics:
      return "ChromeOS.Healthd.TelemetryResult.Graphics";
    case mojom::ProbeCategoryEnum::kDisplay:
      return "ChromeOS.Healthd.TelemetryResult.Display";
    case mojom::ProbeCategoryEnum::kInput:
      return "ChromeOS.Healthd.TelemetryResult.Input";
    case mojom::ProbeCategoryEnum::kAudioHardware:
      return "ChromeOS.Healthd.TelemetryResult.AudioHardware";
    case mojom::ProbeCategoryEnum::kSensor:
      return "ChromeOS.Healthd.TelemetryResult.Sensor";
  }
}

template <typename S>
void SendOneTelemetryResultToUMA(MetricsLibrary* metrics,
                                 mojom::ProbeCategoryEnum category,
                                 const mojo::StructPtr<S>& struct_ptr) {
  std::optional<std::string> metrics_name = GetMetricName(category);
  if (!metrics_name.has_value()) {
    return;
  }

  CrosHealthdTelemetryResult enum_sample;
  if (struct_ptr.is_null() || struct_ptr->is_error()) {
    enum_sample = CrosHealthdTelemetryResult::kError;
  } else {
    enum_sample = CrosHealthdTelemetryResult::kSuccess;
  }

  bool result = metrics->SendEnumToUMA(metrics_name.value(), enum_sample);
  if (!result) {
    LOG(ERROR) << "Failed to send telemetry result of " << category
               << " to UMA.";
  }
}

}  // namespace

void SendTelemetryResultToUMA(
    const std::set<mojom::ProbeCategoryEnum>& requested_categories,
    const mojom::TelemetryInfoPtr& info) {
  if (info.is_null()) {
    LOG(WARNING) << "Cannot send a null telemetry result to UMA.";
    return;
  }

  MetricsLibrary metrics;

  for (const auto category : requested_categories) {
    switch (category) {
      case mojom::ProbeCategoryEnum::kUnknown: {
        // No result to send for an unknown category. Skip it.
        break;
      }
      case mojom::ProbeCategoryEnum::kBattery: {
        SendOneTelemetryResultToUMA(&metrics, category, info->battery_result);
        break;
      }
      case mojom::ProbeCategoryEnum::kCpu: {
        SendOneTelemetryResultToUMA(&metrics, category, info->cpu_result);
        break;
      }
      case mojom::ProbeCategoryEnum::kNonRemovableBlockDevices: {
        SendOneTelemetryResultToUMA(&metrics, category,
                                    info->block_device_result);
        break;
      }
      case mojom::ProbeCategoryEnum::kTimezone: {
        SendOneTelemetryResultToUMA(&metrics, category, info->timezone_result);
        break;
      }
      case mojom::ProbeCategoryEnum::kMemory: {
        SendOneTelemetryResultToUMA(&metrics, category, info->memory_result);
        break;
      }
      case mojom::ProbeCategoryEnum::kBacklight: {
        SendOneTelemetryResultToUMA(&metrics, category, info->backlight_result);
        break;
      }
      case mojom::ProbeCategoryEnum::kFan: {
        SendOneTelemetryResultToUMA(&metrics, category, info->fan_result);
        break;
      }
      case mojom::ProbeCategoryEnum::kStatefulPartition: {
        SendOneTelemetryResultToUMA(&metrics, category,
                                    info->stateful_partition_result);
        break;
      }
      case mojom::ProbeCategoryEnum::kBluetooth: {
        SendOneTelemetryResultToUMA(&metrics, category, info->bluetooth_result);
        break;
      }
      case mojom::ProbeCategoryEnum::kSystem: {
        SendOneTelemetryResultToUMA(&metrics, category, info->system_result);
        break;
      }
      case mojom::ProbeCategoryEnum::kNetwork: {
        SendOneTelemetryResultToUMA(&metrics, category, info->network_result);
        break;
      }
      case mojom::ProbeCategoryEnum::kAudio: {
        SendOneTelemetryResultToUMA(&metrics, category, info->audio_result);
        break;
      }
      case mojom::ProbeCategoryEnum::kBootPerformance: {
        SendOneTelemetryResultToUMA(&metrics, category,
                                    info->boot_performance_result);
        break;
      }
      case mojom::ProbeCategoryEnum::kBus: {
        SendOneTelemetryResultToUMA(&metrics, category, info->bus_result);
        break;
      }
      case mojom::ProbeCategoryEnum::kTpm: {
        SendOneTelemetryResultToUMA(&metrics, category, info->tpm_result);
        break;
      }
      case mojom::ProbeCategoryEnum::kNetworkInterface: {
        SendOneTelemetryResultToUMA(&metrics, category,
                                    info->network_interface_result);
        break;
      }
      case mojom::ProbeCategoryEnum::kGraphics: {
        SendOneTelemetryResultToUMA(&metrics, category, info->graphics_result);
        break;
      }
      case mojom::ProbeCategoryEnum::kDisplay: {
        SendOneTelemetryResultToUMA(&metrics, category, info->display_result);
        break;
      }
      case mojom::ProbeCategoryEnum::kInput: {
        SendOneTelemetryResultToUMA(&metrics, category, info->input_result);
        break;
      }
      case mojom::ProbeCategoryEnum::kAudioHardware: {
        SendOneTelemetryResultToUMA(&metrics, category,
                                    info->audio_hardware_result);
        break;
      }
      case mojom::ProbeCategoryEnum::kSensor: {
        SendOneTelemetryResultToUMA(&metrics, category, info->sensor_result);
        break;
      }
    }
  }
}

}  // namespace diagnostics
