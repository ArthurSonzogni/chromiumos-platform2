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

#include "diagnostics/cros_healthd/utils/metrics_utils_constants.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

std::optional<std::string> GetMetricName(mojom::ProbeCategoryEnum category) {
  switch (category) {
    case mojom::ProbeCategoryEnum::kUnknown:
      // No metric name for the unknown category.
      return std::nullopt;
    case mojom::ProbeCategoryEnum::kBattery:
      return metrics_name::kTelemetryResultBattery;
    case mojom::ProbeCategoryEnum::kCpu:
      return metrics_name::kTelemetryResultCpu;
    case mojom::ProbeCategoryEnum::kNonRemovableBlockDevices:
      return metrics_name::kTelemetryResultBlockDevice;
    case mojom::ProbeCategoryEnum::kTimezone:
      return metrics_name::kTelemetryResultTimezone;
    case mojom::ProbeCategoryEnum::kMemory:
      return metrics_name::kTelemetryResultMemory;
    case mojom::ProbeCategoryEnum::kBacklight:
      return metrics_name::kTelemetryResultBacklight;
    case mojom::ProbeCategoryEnum::kFan:
      return metrics_name::kTelemetryResultFan;
    case mojom::ProbeCategoryEnum::kStatefulPartition:
      return metrics_name::kTelemetryResultStatefulPartition;
    case mojom::ProbeCategoryEnum::kBluetooth:
      return metrics_name::kTelemetryResultBluetooth;
    case mojom::ProbeCategoryEnum::kSystem:
      return metrics_name::kTelemetryResultSystem;
    case mojom::ProbeCategoryEnum::kNetwork:
      return metrics_name::kTelemetryResultNetwork;
    case mojom::ProbeCategoryEnum::kAudio:
      return metrics_name::kTelemetryResultAudio;
    case mojom::ProbeCategoryEnum::kBootPerformance:
      return metrics_name::kTelemetryResultBootPerformance;
    case mojom::ProbeCategoryEnum::kBus:
      return metrics_name::kTelemetryResultBus;
    case mojom::ProbeCategoryEnum::kTpm:
      return metrics_name::kTelemetryResultTpm;
    case mojom::ProbeCategoryEnum::kNetworkInterface:
      return metrics_name::kTelemetryResultNetworkInterface;
    case mojom::ProbeCategoryEnum::kGraphics:
      return metrics_name::kTelemetryResultGraphics;
    case mojom::ProbeCategoryEnum::kDisplay:
      return metrics_name::kTelemetryResultDisplay;
    case mojom::ProbeCategoryEnum::kInput:
      return metrics_name::kTelemetryResultInput;
    case mojom::ProbeCategoryEnum::kAudioHardware:
      return metrics_name::kTelemetryResultAudioHardware;
    case mojom::ProbeCategoryEnum::kSensor:
      return metrics_name::kTelemetryResultSensor;
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
