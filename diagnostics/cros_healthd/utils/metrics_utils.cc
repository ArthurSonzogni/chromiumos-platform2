// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/metrics_utils.h"

#include <optional>
#include <set>
#include <string>
#include <utility>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <metrics/metrics_library.h>
#include <mojo/public/cpp/bindings/struct_ptr.h>

#include "diagnostics/cros_healthd/utils/metrics_utils_constants.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

std::string GetMetricName(mojom::ProbeCategoryEnum category) {
  switch (category) {
    case mojom::ProbeCategoryEnum::kUnknown:
      // `kUnknown` should have been filtered out in
      // `SendTelemetryResultToUMA()`.
      NOTREACHED_NORETURN();
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
    case mojom::ProbeCategoryEnum::kThermal:
      return metrics_name::kTelemetryResultThermal;
  }
}

std::optional<std::string> GetMetricName(mojom::DiagnosticRoutineEnum routine) {
  switch (routine) {
    case mojom::DiagnosticRoutineEnum::kUnknown:
      // No metric name for the unknown routine.
      return std::nullopt;
    case mojom::DiagnosticRoutineEnum::kBatteryCapacity:
      return metrics_name::kDiagnosticResultBatteryCapacity;
    case mojom::DiagnosticRoutineEnum::kBatteryHealth:
      return metrics_name::kDiagnosticResultBatteryHealth;
    case mojom::DiagnosticRoutineEnum::kUrandom:
      return metrics_name::kDiagnosticResultUrandom;
    case mojom::DiagnosticRoutineEnum::kSmartctlCheck:
      return metrics_name::kDiagnosticResultSmartctlCheck;
    case mojom::DiagnosticRoutineEnum::kAcPower:
      return metrics_name::kDiagnosticResultAcPower;
    case mojom::DiagnosticRoutineEnum::kCpuCache:
      return metrics_name::kDiagnosticResultCpuCache;
    case mojom::DiagnosticRoutineEnum::kCpuStress:
      return metrics_name::kDiagnosticResultCpuStress;
    case mojom::DiagnosticRoutineEnum::kFloatingPointAccuracy:
      return metrics_name::kDiagnosticResultFloatingPointAccuracy;
    case mojom::DiagnosticRoutineEnum::DEPRECATED_kNvmeWearLevel:
      return metrics_name::kDiagnosticResultNvmeWearLevel;
    case mojom::DiagnosticRoutineEnum::kNvmeSelfTest:
      return metrics_name::kDiagnosticResultNvmeSelfTest;
    case mojom::DiagnosticRoutineEnum::kDiskRead:
      return metrics_name::kDiagnosticResultDiskRead;
    case mojom::DiagnosticRoutineEnum::kPrimeSearch:
      return metrics_name::kDiagnosticResultPrimeSearch;
    case mojom::DiagnosticRoutineEnum::kBatteryDischarge:
      return metrics_name::kDiagnosticResultBatteryDischarge;
    case mojom::DiagnosticRoutineEnum::kBatteryCharge:
      return metrics_name::kDiagnosticResultBatteryCharge;
    case mojom::DiagnosticRoutineEnum::kMemory:
      return metrics_name::kDiagnosticResultMemory;
    case mojom::DiagnosticRoutineEnum::kLanConnectivity:
      return metrics_name::kDiagnosticResultLanConnectivity;
    case mojom::DiagnosticRoutineEnum::kSignalStrength:
      return metrics_name::kDiagnosticResultSignalStrength;
    case mojom::DiagnosticRoutineEnum::kGatewayCanBePinged:
      return metrics_name::kDiagnosticResultGatewayCanBePinged;
    case mojom::DiagnosticRoutineEnum::kHasSecureWiFiConnection:
      return metrics_name::kDiagnosticResultHasSecureWiFiConnection;
    case mojom::DiagnosticRoutineEnum::kDnsResolverPresent:
      return metrics_name::kDiagnosticResultDnsResolverPresent;
    case mojom::DiagnosticRoutineEnum::kDnsLatency:
      return metrics_name::kDiagnosticResultDnsLatency;
    case mojom::DiagnosticRoutineEnum::kDnsResolution:
      return metrics_name::kDiagnosticResultDnsResolution;
    case mojom::DiagnosticRoutineEnum::kCaptivePortal:
      return metrics_name::kDiagnosticResultCaptivePortal;
    case mojom::DiagnosticRoutineEnum::kHttpFirewall:
      return metrics_name::kDiagnosticResultHttpFirewall;
    case mojom::DiagnosticRoutineEnum::kHttpsFirewall:
      return metrics_name::kDiagnosticResultHttpsFirewall;
    case mojom::DiagnosticRoutineEnum::kHttpsLatency:
      return metrics_name::kDiagnosticResultHttpsLatency;
    case mojom::DiagnosticRoutineEnum::kVideoConferencing:
      return metrics_name::kDiagnosticResultVideoConferencing;
    case mojom::DiagnosticRoutineEnum::kArcHttp:
      return metrics_name::kDiagnosticResultArcHttp;
    case mojom::DiagnosticRoutineEnum::kArcPing:
      return metrics_name::kDiagnosticResultArcPing;
    case mojom::DiagnosticRoutineEnum::kArcDnsResolution:
      return metrics_name::kDiagnosticResultArcDnsResolution;
    case mojom::DiagnosticRoutineEnum::kSensitiveSensor:
      return metrics_name::kDiagnosticResultSensitiveSensor;
    case mojom::DiagnosticRoutineEnum::kFingerprint:
      return metrics_name::kDiagnosticResultFingerprint;
    case mojom::DiagnosticRoutineEnum::kFingerprintAlive:
      return metrics_name::kDiagnosticResultFingerprintAlive;
    case mojom::DiagnosticRoutineEnum::kPrivacyScreen:
      return metrics_name::kDiagnosticResultPrivacyScreen;
    case mojom::DiagnosticRoutineEnum::kLedLitUp:
      return metrics_name::kDiagnosticResultLedLitUp;
    case mojom::DiagnosticRoutineEnum::kSmartctlCheckWithPercentageUsed:
      return metrics_name::kDiagnosticResultSmartctlCheckWithPercentageUsed;
    case mojom::DiagnosticRoutineEnum::kEmmcLifetime:
      return metrics_name::kDiagnosticResultEmmcLifetime;
    case mojom::DiagnosticRoutineEnum::DEPRECATED_kAudioSetVolume:
      return metrics_name::kDiagnosticResultAudioSetVolume;
    case mojom::DiagnosticRoutineEnum::DEPRECATED_kAudioSetGain:
      return metrics_name::kDiagnosticResultAudioSetGain;
    case mojom::DiagnosticRoutineEnum::kBluetoothPower:
      return metrics_name::kDiagnosticResultBluetoothPower;
    case mojom::DiagnosticRoutineEnum::kBluetoothDiscovery:
      return metrics_name::kDiagnosticResultBluetoothDiscovery;
    case mojom::DiagnosticRoutineEnum::kBluetoothScanning:
      return metrics_name::kDiagnosticResultBluetoothScanning;
    case mojom::DiagnosticRoutineEnum::kBluetoothPairing:
      return metrics_name::kDiagnosticResultBluetoothPairing;
    case mojom::DiagnosticRoutineEnum::kPowerButton:
      return metrics_name::kDiagnosticResultPowerButton;
    case mojom::DiagnosticRoutineEnum::kAudioDriver:
      return metrics_name::kDiagnosticResultAudioDriver;
    case mojom::DiagnosticRoutineEnum::kUfsLifetime:
      return metrics_name::kDiagnosticResultUfsLifetime;
    case mojom::DiagnosticRoutineEnum::kFan:
      return metrics_name::kDiagnosticResultFan;
  }
}

std::optional<metrics_enum::CrosHealthdDiagnosticResult>
ConvertDiagnosticStatusToUMAEnum(mojom::DiagnosticRoutineStatusEnum status) {
  switch (status) {
    case mojom::DiagnosticRoutineStatusEnum::kPassed:
      return metrics_enum::CrosHealthdDiagnosticResult::kPassed;
    case mojom::DiagnosticRoutineStatusEnum::kFailed:
      return metrics_enum::CrosHealthdDiagnosticResult::kFailed;
    case mojom::DiagnosticRoutineStatusEnum::kError:
      return metrics_enum::CrosHealthdDiagnosticResult::kError;
    case mojom::DiagnosticRoutineStatusEnum::kCancelled:
      return metrics_enum::CrosHealthdDiagnosticResult::kCancelled;
    case mojom::DiagnosticRoutineStatusEnum::kFailedToStart:
      return metrics_enum::CrosHealthdDiagnosticResult::kFailedToStart;
    case mojom::DiagnosticRoutineStatusEnum::kRemoved:
      return metrics_enum::CrosHealthdDiagnosticResult::kRemoved;
    case mojom::DiagnosticRoutineStatusEnum::kUnsupported:
      return metrics_enum::CrosHealthdDiagnosticResult::kUnsupported;
    case mojom::DiagnosticRoutineStatusEnum::kNotRun:
      return metrics_enum::CrosHealthdDiagnosticResult::kNotRun;
    // Non-terminal status.
    case mojom::DiagnosticRoutineStatusEnum::kUnknown:
    case mojom::DiagnosticRoutineStatusEnum::kReady:
    case mojom::DiagnosticRoutineStatusEnum::kRunning:
    case mojom::DiagnosticRoutineStatusEnum::kWaiting:
    case mojom::DiagnosticRoutineStatusEnum::kCancelling:
      return std::nullopt;
  }
}

std::optional<metrics_enum::CrosHealthdEventCategory>
ConvertEventCategoryToUMAEnum(mojom::EventCategoryEnum event_category) {
  switch (event_category) {
    case mojom::EventCategoryEnum::kUnmappedEnumField:
      return std::nullopt;
    case mojom::EventCategoryEnum::kUsb:
      return metrics_enum::CrosHealthdEventCategory::kUsb;
    case mojom::EventCategoryEnum::kThunderbolt:
      return metrics_enum::CrosHealthdEventCategory::kThunderbolt;
    case mojom::EventCategoryEnum::kLid:
      return metrics_enum::CrosHealthdEventCategory::kLid;
    case mojom::EventCategoryEnum::kBluetooth:
      return metrics_enum::CrosHealthdEventCategory::kBluetooth;
    case mojom::EventCategoryEnum::kPower:
      return metrics_enum::CrosHealthdEventCategory::kPower;
    case mojom::EventCategoryEnum::kAudio:
      return metrics_enum::CrosHealthdEventCategory::kAudio;
    case mojom::EventCategoryEnum::kAudioJack:
      return metrics_enum::CrosHealthdEventCategory::kAudioJack;
    case mojom::EventCategoryEnum::kSdCard:
      return metrics_enum::CrosHealthdEventCategory::kSdCard;
    case mojom::EventCategoryEnum::kNetwork:
      return metrics_enum::CrosHealthdEventCategory::kNetwork;
    case mojom::EventCategoryEnum::kKeyboardDiagnostic:
      return metrics_enum::CrosHealthdEventCategory::kKeyboardDiagnostic;
    case mojom::EventCategoryEnum::kTouchpad:
      return metrics_enum::CrosHealthdEventCategory::kTouchpad;
    case mojom::EventCategoryEnum::kExternalDisplay:
      return metrics_enum::CrosHealthdEventCategory::kExternalDisplay;
    case mojom::EventCategoryEnum::kTouchscreen:
      return metrics_enum::CrosHealthdEventCategory::kTouchscreen;
    case mojom::EventCategoryEnum::kStylusGarage:
      return metrics_enum::CrosHealthdEventCategory::kStylusGarage;
    case mojom::EventCategoryEnum::kStylus:
      return metrics_enum::CrosHealthdEventCategory::kStylus;
    case mojom::EventCategoryEnum::kCrash:
      return metrics_enum::CrosHealthdEventCategory::kCrash;
  }
}

std::optional<metrics_enum::CrosHealthdRoutineCategory>
ConvertRoutineCategoryToUMAEnum(mojom::RoutineArgument::Tag routine_category) {
  switch (routine_category) {
    case mojom::RoutineArgument::Tag::kUnrecognizedArgument:
      return std::nullopt;
    case mojom::RoutineArgument::Tag::kPrimeSearch:
      return metrics_enum::CrosHealthdRoutineCategory::kPrimeSearch;
    case mojom::RoutineArgument::Tag::kFloatingPoint:
      return metrics_enum::CrosHealthdRoutineCategory::kFloatingPoint;
    case mojom::RoutineArgument::Tag::kMemory:
      return metrics_enum::CrosHealthdRoutineCategory::kMemory;
    case mojom::RoutineArgument::Tag::kAudioDriver:
      return metrics_enum::CrosHealthdRoutineCategory::kAudioDriver;
    case mojom::RoutineArgument::Tag::kCpuStress:
      return metrics_enum::CrosHealthdRoutineCategory::kCpuStress;
    case mojom::RoutineArgument::Tag::kUfsLifetime:
      return metrics_enum::CrosHealthdRoutineCategory::kUfsLifetime;
    case mojom::RoutineArgument::Tag::kDiskRead:
      return metrics_enum::CrosHealthdRoutineCategory::kDiskRead;
    case mojom::RoutineArgument::Tag::kCpuCache:
      return metrics_enum::CrosHealthdRoutineCategory::kCpuCache;
    case mojom::RoutineArgument::Tag::kVolumeButton:
      return metrics_enum::CrosHealthdRoutineCategory::kVolumeButton;
    case mojom::RoutineArgument::Tag::kLedLitUp:
      return metrics_enum::CrosHealthdRoutineCategory::kLedLitUp;
    case mojom::RoutineArgument::Tag::kBluetoothPower:
      return metrics_enum::CrosHealthdRoutineCategory::kBluetoothPower;
    case mojom::RoutineArgument::Tag::kBluetoothDiscovery:
      return metrics_enum::CrosHealthdRoutineCategory::kBluetoothDiscovery;
    case mojom::RoutineArgument::Tag::kFan:
      return metrics_enum::CrosHealthdRoutineCategory::kFan;
    case mojom::RoutineArgument::Tag::kBluetoothScanning:
      return metrics_enum::CrosHealthdRoutineCategory::kBluetoothScanning;
    case mojom::RoutineArgument::Tag::kBluetoothPairing:
      return metrics_enum::CrosHealthdRoutineCategory::kBluetoothPairing;
    case mojom::RoutineArgument::Tag::kCameraAvailability:
      return metrics_enum::CrosHealthdRoutineCategory::kCameraAvailability;
    case mojom::RoutineArgument::Tag::kUrandom:
      return metrics_enum::CrosHealthdRoutineCategory::kUrandom;
    case mojom::RoutineArgument::Tag::kNetworkBandwidth:
      return metrics_enum::CrosHealthdRoutineCategory::kNetworkBandwidth;
    case mojom::RoutineArgument::Tag::kSensitiveSensor:
      return metrics_enum::CrosHealthdRoutineCategory::kSensitiveSensor;
    case mojom::RoutineArgument::Tag::kCameraFrameAnalysis:
      return metrics_enum::CrosHealthdRoutineCategory::kCameraFrameAnalysis;
    case mojom::RoutineArgument::Tag::kBatteryDischarge:
      return metrics_enum::CrosHealthdRoutineCategory::kBatteryDischarge;
  }
}

template <typename S>
void SendOneTelemetryResultToUMA(MetricsLibraryInterface* metrics,
                                 mojom::ProbeCategoryEnum category,
                                 const mojo::StructPtr<S>& struct_ptr) {
  metrics_enum::CrosHealthdTelemetryResult enum_sample;
  if (struct_ptr.is_null() || struct_ptr->is_error()) {
    enum_sample = metrics_enum::CrosHealthdTelemetryResult::kError;
  } else {
    enum_sample = metrics_enum::CrosHealthdTelemetryResult::kSuccess;
  }

  metrics->SendEnumToUMA(GetMetricName(category), enum_sample);
}

}  // namespace

base::RepeatingCallback<void(mojom::DiagnosticRoutineStatusEnum)>
InvokeOnTerminalStatus(
    base::OnceCallback<void(mojom::DiagnosticRoutineStatusEnum)>
        on_terminal_status_cb) {
  return base::BindRepeating(
      [](base::OnceCallback<void(mojom::DiagnosticRoutineStatusEnum)>& callback,
         mojom::DiagnosticRoutineStatusEnum status) {
        switch (status) {
          // Non-terminal status.
          case mojom::DiagnosticRoutineStatusEnum::kUnknown:
          case mojom::DiagnosticRoutineStatusEnum::kReady:
          case mojom::DiagnosticRoutineStatusEnum::kRunning:
          case mojom::DiagnosticRoutineStatusEnum::kWaiting:
          case mojom::DiagnosticRoutineStatusEnum::kCancelling:
            return;
          // Terminal status.
          case mojom::DiagnosticRoutineStatusEnum::kPassed:
          case mojom::DiagnosticRoutineStatusEnum::kFailed:
          case mojom::DiagnosticRoutineStatusEnum::kError:
          case mojom::DiagnosticRoutineStatusEnum::kCancelled:
          case mojom::DiagnosticRoutineStatusEnum::kFailedToStart:
          case mojom::DiagnosticRoutineStatusEnum::kRemoved:
          case mojom::DiagnosticRoutineStatusEnum::kUnsupported:
          case mojom::DiagnosticRoutineStatusEnum::kNotRun:
            // |callback| will be null if it has already been called.
            if (callback) {
              std::move(callback).Run(status);
            }
            break;
        }
      },
      base::OwnedRef(std::move(on_terminal_status_cb)));
}

void SendTelemetryResultToUMA(
    MetricsLibraryInterface* metrics,
    const std::set<mojom::ProbeCategoryEnum>& requested_categories,
    const mojom::TelemetryInfoPtr& info) {
  CHECK(info);

  for (const auto category : requested_categories) {
    switch (category) {
      case mojom::ProbeCategoryEnum::kUnknown:
        // No result to send for an unknown category. Skip it.
        break;
      case mojom::ProbeCategoryEnum::kBattery:
        SendOneTelemetryResultToUMA(metrics, category, info->battery_result);
        break;
      case mojom::ProbeCategoryEnum::kCpu:
        SendOneTelemetryResultToUMA(metrics, category, info->cpu_result);
        break;
      case mojom::ProbeCategoryEnum::kNonRemovableBlockDevices:
        SendOneTelemetryResultToUMA(metrics, category,
                                    info->block_device_result);
        break;
      case mojom::ProbeCategoryEnum::kTimezone:
        SendOneTelemetryResultToUMA(metrics, category, info->timezone_result);
        break;
      case mojom::ProbeCategoryEnum::kMemory:
        SendOneTelemetryResultToUMA(metrics, category, info->memory_result);
        break;
      case mojom::ProbeCategoryEnum::kBacklight:
        SendOneTelemetryResultToUMA(metrics, category, info->backlight_result);
        break;
      case mojom::ProbeCategoryEnum::kFan:
        SendOneTelemetryResultToUMA(metrics, category, info->fan_result);
        break;
      case mojom::ProbeCategoryEnum::kStatefulPartition:
        SendOneTelemetryResultToUMA(metrics, category,
                                    info->stateful_partition_result);
        break;
      case mojom::ProbeCategoryEnum::kBluetooth:
        SendOneTelemetryResultToUMA(metrics, category, info->bluetooth_result);
        break;
      case mojom::ProbeCategoryEnum::kSystem:
        SendOneTelemetryResultToUMA(metrics, category, info->system_result);
        break;
      case mojom::ProbeCategoryEnum::kNetwork:
        SendOneTelemetryResultToUMA(metrics, category, info->network_result);
        break;
      case mojom::ProbeCategoryEnum::kAudio:
        SendOneTelemetryResultToUMA(metrics, category, info->audio_result);
        break;
      case mojom::ProbeCategoryEnum::kBootPerformance:
        SendOneTelemetryResultToUMA(metrics, category,
                                    info->boot_performance_result);
        break;
      case mojom::ProbeCategoryEnum::kBus:
        SendOneTelemetryResultToUMA(metrics, category, info->bus_result);
        break;
      case mojom::ProbeCategoryEnum::kTpm:
        SendOneTelemetryResultToUMA(metrics, category, info->tpm_result);
        break;
      case mojom::ProbeCategoryEnum::kNetworkInterface:
        SendOneTelemetryResultToUMA(metrics, category,
                                    info->network_interface_result);
        break;
      case mojom::ProbeCategoryEnum::kGraphics:
        SendOneTelemetryResultToUMA(metrics, category, info->graphics_result);
        break;
      case mojom::ProbeCategoryEnum::kDisplay:
        SendOneTelemetryResultToUMA(metrics, category, info->display_result);
        break;
      case mojom::ProbeCategoryEnum::kInput:
        SendOneTelemetryResultToUMA(metrics, category, info->input_result);
        break;
      case mojom::ProbeCategoryEnum::kAudioHardware:
        SendOneTelemetryResultToUMA(metrics, category,
                                    info->audio_hardware_result);
        break;
      case mojom::ProbeCategoryEnum::kSensor:
        SendOneTelemetryResultToUMA(metrics, category, info->sensor_result);
        break;
      case mojom::ProbeCategoryEnum::kThermal:
        SendOneTelemetryResultToUMA(metrics, category, info->thermal_result);
        break;
    }
  }
}

void SendDiagnosticResultToUMA(MetricsLibraryInterface* metrics,
                               mojom::DiagnosticRoutineEnum routine,
                               mojom::DiagnosticRoutineStatusEnum status) {
  std::optional<std::string> metrics_name = GetMetricName(routine);
  if (!metrics_name.has_value()) {
    return;
  }

  std::optional<metrics_enum::CrosHealthdDiagnosticResult> result_enum =
      ConvertDiagnosticStatusToUMAEnum(status);
  if (!result_enum.has_value()) {
    return;
  }

  metrics->SendEnumToUMA(metrics_name.value(), result_enum.value());
}

void SendEventSubscriptionUsageToUMA(MetricsLibraryInterface* metrics,
                                     mojom::EventCategoryEnum category) {
  std::optional<metrics_enum::CrosHealthdEventCategory> category_enum =
      ConvertEventCategoryToUMAEnum(category);
  if (!category_enum.has_value()) {
    // No need to record unrecognized category.
    return;
  }

  metrics->SendEnumToUMA(metrics_name::kEventSubscription,
                         category_enum.value());
}

void SendRoutineCreationUsageToUMA(
    MetricsLibraryInterface* metrics,
    ash::cros_healthd::mojom::RoutineArgument::Tag category) {
  std::optional<metrics_enum::CrosHealthdRoutineCategory> category_enum =
      ConvertRoutineCategoryToUMAEnum(category);
  if (!category_enum.has_value()) {
    // No need to record unrecognized category.
    return;
  }

  metrics->SendEnumToUMA(metrics_name::kRoutineCreation, category_enum.value());
}

}  // namespace diagnostics
