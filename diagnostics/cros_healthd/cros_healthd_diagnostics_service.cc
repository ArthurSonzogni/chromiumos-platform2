// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/cros_healthd_diagnostics_service.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/time/time.h>
#include <chromeos/mojo/service_constants.h>
#include <metrics/metrics_library.h>

#include "diagnostics/cros_healthd/routine_adapter.h"
#include "diagnostics/cros_healthd/routines/diag_routine.h"
#include "diagnostics/cros_healthd/system/floss_controller.h"
#include "diagnostics/cros_healthd/system/ground_truth.h"
#include "diagnostics/cros_healthd/system/mojo_service.h"
#include "diagnostics/cros_healthd/system/system_config_interface.h"
#include "diagnostics/cros_healthd/utils/callback_barrier.h"
#include "diagnostics/cros_healthd/utils/dbus_utils.h"
#include "diagnostics/cros_healthd/utils/metrics_utils.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"
#include "diagnostics/mojom/public/nullable_primitives.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

void SetErrorRoutineUpdate(const std::string& status_message,
                           mojom::RoutineUpdate* response) {
  auto noninteractive_update = mojom::NonInteractiveRoutineUpdate::New();
  noninteractive_update->status = mojom::DiagnosticRoutineStatusEnum::kError;
  noninteractive_update->status_message = status_message;
  response->routine_update_union =
      mojom::RoutineUpdateUnion::NewNoninteractiveUpdate(
          std::move(noninteractive_update));
  response->progress_percent = 0;
}

void SendResultToUMA(mojom::DiagnosticRoutineEnum routine,
                     mojom::DiagnosticRoutineStatusEnum status) {
  MetricsLibrary metrics;
  SendDiagnosticResultToUMA(&metrics, routine, status);
}

mojom::DiskReadTypeEnum Convert(mojom::DiskReadRoutineTypeEnum type) {
  switch (type) {
    case mojom::DiskReadRoutineTypeEnum::kLinearRead:
      return mojom::DiskReadTypeEnum::kLinearRead;
    case mojom::DiskReadRoutineTypeEnum::kRandomRead:
      return mojom::DiskReadTypeEnum::kRandomRead;
    case mojom::DiskReadRoutineTypeEnum::kUnmappedEnumField:
      return mojom::DiskReadTypeEnum::kUnmappedEnumField;
  }
}

void ReportUnsupportedRoutine(
    mojom::DiagnosticRoutineEnum routine_enum,
    base::OnceCallback<void(mojom::RunRoutineResponsePtr)> callback) {
  LOG(ERROR) << routine_enum << " is not supported on this device";
  SendResultToUMA(routine_enum,
                  mojom::DiagnosticRoutineStatusEnum::kUnsupported);
  std::move(callback).Run(mojom::RunRoutineResponse::New(
      mojom::kFailedToStartId,
      mojom::DiagnosticRoutineStatusEnum::kUnsupported));
}

void CheckFlossEnabled(
    FlossController* floss_controller,
    base::OnceCallback<void(brillo::Error*, bool)> callback) {
  CHECK(floss_controller);
  const auto manager = floss_controller->GetManager();
  if (!manager) {
    std::move(callback).Run(nullptr, /*floss_enabled=*/false);
    return;
  }
  auto [on_success, on_error] = SplitDbusCallback(std::move(callback));
  manager->GetFlossEnabledAsync(std::move(on_success), std::move(on_error));
}

}  // namespace

CrosHealthdDiagnosticsService::CrosHealthdDiagnosticsService(
    Context* context,
    CrosHealthdRoutineFactory* routine_factory,
    ash::cros_healthd::mojom::CrosHealthdRoutinesService* routine_service)
    : context_(context),
      routine_factory_(routine_factory),
      routine_service_(routine_service) {
  CHECK(context_);
  CHECK(routine_factory_);
  CHECK(routine_service_);
  ground_truth_ = std::make_unique<GroundTruth>(context_);

  // Service is ready after available routines are populated.
  PopulateAvailableRoutines(
      base::BindOnce(&CrosHealthdDiagnosticsService::OnServiceReady,
                     weak_ptr_factory_.GetWeakPtr()));
}

CrosHealthdDiagnosticsService::~CrosHealthdDiagnosticsService() = default;

void CrosHealthdDiagnosticsService::GetAvailableRoutines(
    GetAvailableRoutinesCallback callback) {
  std::move(callback).Run(std::vector<mojom::DiagnosticRoutineEnum>(
      available_routines_.begin(), available_routines_.end()));
}

void CrosHealthdDiagnosticsService::GetRoutineUpdate(
    int32_t id,
    mojom::DiagnosticRoutineCommandEnum command,
    bool include_output,
    GetRoutineUpdateCallback callback) {
  mojom::RoutineUpdate update{0, mojo::ScopedHandle(),
                              mojom::RoutineUpdateUnionPtr()};

  auto itr = active_routines_.find(id);
  if (itr == active_routines_.end()) {
    LOG(ERROR) << "Bad id in GetRoutineUpdateRequest: " << id;
    SetErrorRoutineUpdate("Specified routine does not exist.", &update);
    std::move(callback).Run(mojom::RoutineUpdate::New(
        update.progress_percent, std::move(update.output),
        std::move(update.routine_update_union)));
    return;
  }

  auto* routine = itr->second.get();
  switch (command) {
    case mojom::DiagnosticRoutineCommandEnum::kContinue:
      routine->Resume();
      break;
    case mojom::DiagnosticRoutineCommandEnum::kCancel:
      routine->Cancel();
      break;
    case mojom::DiagnosticRoutineCommandEnum::kGetStatus:
      // Retrieving the status and output of a routine is handled below.
      break;
    case mojom::DiagnosticRoutineCommandEnum::kRemove:
      routine->PopulateStatusUpdate(&update, include_output);
      if (update.routine_update_union->is_noninteractive_update()) {
        update.routine_update_union->get_noninteractive_update()->status =
            mojom::DiagnosticRoutineStatusEnum::kRemoved;
      }
      active_routines_.erase(itr);
      // |routine| is invalid at this point!
      std::move(callback).Run(mojom::RoutineUpdate::New(
          update.progress_percent, std::move(update.output),
          std::move(update.routine_update_union)));
      return;
    case mojom::DiagnosticRoutineCommandEnum::kUnknown:
      LOG(ERROR) << "Get unknown command";
      break;
  }

  routine->PopulateStatusUpdate(&update, include_output);
  std::move(callback).Run(mojom::RoutineUpdate::New(
      update.progress_percent, std::move(update.output),
      std::move(update.routine_update_union)));
}

void CrosHealthdDiagnosticsService::RunAcPowerRoutine(
    mojom::AcPowerStatusEnum expected_status,
    const std::optional<std::string>& expected_power_type,
    RunAcPowerRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeAcPowerRoutine(expected_status,
                                                  expected_power_type),
             mojom::DiagnosticRoutineEnum::kAcPower, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunBatteryCapacityRoutine(
    RunBatteryCapacityRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeBatteryCapacityRoutine(),
             mojom::DiagnosticRoutineEnum::kBatteryCapacity,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunBatteryChargeRoutine(
    uint32_t length_seconds,
    uint32_t minimum_charge_percent_required,
    RunBatteryChargeRoutineCallback callback) {
  RunRoutine(
      routine_factory_->MakeBatteryChargeRoutine(
          base::Seconds(length_seconds), minimum_charge_percent_required),
      mojom::DiagnosticRoutineEnum::kBatteryCharge, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunBatteryDischargeRoutine(
    uint32_t length_seconds,
    uint32_t maximum_discharge_percent_allowed,
    RunBatteryDischargeRoutineCallback callback) {
  RunRoutine(
      routine_factory_->MakeBatteryDischargeRoutine(
          base::Seconds(length_seconds), maximum_discharge_percent_allowed),
      mojom::DiagnosticRoutineEnum::kBatteryDischarge, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunBatteryHealthRoutine(
    RunBatteryHealthRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeBatteryHealthRoutine(),
             mojom::DiagnosticRoutineEnum::kBatteryHealth, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunCaptivePortalRoutine(
    RunCaptivePortalRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeCaptivePortalRoutine(),
             mojom::DiagnosticRoutineEnum::kCaptivePortal, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunCpuCacheRoutine(
    mojom::NullableUint32Ptr length_seconds,
    RunCpuCacheRoutineCallback callback) {
  std::optional<base::TimeDelta> exec_duration;
  if (!length_seconds.is_null()) {
    exec_duration = base::Seconds(length_seconds->value);
  }

  auto args = mojom::RoutineArgument::NewCpuCache(
      mojom::CpuCacheRoutineArgument::New(exec_duration));
  RunRoutineWithAdapter(std::move(args),
                        mojom::DiagnosticRoutineEnum::kCpuCache,
                        std::move(callback));
}

void CrosHealthdDiagnosticsService::RunCpuStressRoutine(
    mojom::NullableUint32Ptr length_seconds,
    RunCpuStressRoutineCallback callback) {
  std::optional<base::TimeDelta> exec_duration;
  if (!length_seconds.is_null()) {
    exec_duration = base::Seconds(length_seconds->value);
  }

  auto args = mojom::RoutineArgument::NewCpuStress(
      mojom::CpuStressRoutineArgument::New(exec_duration));
  RunRoutineWithAdapter(std::move(args),
                        mojom::DiagnosticRoutineEnum::kCpuStress,
                        std::move(callback));
}

void CrosHealthdDiagnosticsService::RunDiskReadRoutine(
    mojom::DiskReadRoutineTypeEnum type,
    uint32_t length_seconds,
    uint32_t file_size_mb,
    RunDiskReadRoutineCallback callback) {
  auto args =
      mojom::RoutineArgument::NewDiskRead(mojom::DiskReadRoutineArgument::New(
          Convert(type), base::Seconds(length_seconds), file_size_mb));
  RunRoutineWithAdapter(std::move(args),
                        mojom::DiagnosticRoutineEnum::kDiskRead,
                        std::move(callback));
}

void CrosHealthdDiagnosticsService::RunDnsLatencyRoutine(
    RunDnsLatencyRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeDnsLatencyRoutine(),
             mojom::DiagnosticRoutineEnum::kDnsLatency, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunDnsResolutionRoutine(
    RunDnsResolutionRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeDnsResolutionRoutine(),
             mojom::DiagnosticRoutineEnum::kDnsResolution, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunDnsResolverPresentRoutine(
    RunDnsResolverPresentRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeDnsResolverPresentRoutine(),
             mojom::DiagnosticRoutineEnum::kDnsResolverPresent,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunFloatingPointAccuracyRoutine(
    mojom::NullableUint32Ptr length_seconds,
    RunFloatingPointAccuracyRoutineCallback callback) {
  std::optional<base::TimeDelta> exec_duration;
  if (!length_seconds.is_null()) {
    exec_duration = base::Seconds(length_seconds->value);
  }

  auto args = mojom::RoutineArgument::NewFloatingPoint(
      mojom::FloatingPointRoutineArgument::New(exec_duration));
  RunRoutineWithAdapter(std::move(args),
                        mojom::DiagnosticRoutineEnum::kFloatingPointAccuracy,
                        std::move(callback));
}

void CrosHealthdDiagnosticsService::RunGatewayCanBePingedRoutine(
    RunGatewayCanBePingedRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeGatewayCanBePingedRoutine(),
             mojom::DiagnosticRoutineEnum::kGatewayCanBePinged,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunHasSecureWiFiConnectionRoutine(
    RunHasSecureWiFiConnectionRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeHasSecureWiFiConnectionRoutine(),
             mojom::DiagnosticRoutineEnum::kHasSecureWiFiConnection,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunHttpFirewallRoutine(
    RunHttpFirewallRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeHttpFirewallRoutine(),
             mojom::DiagnosticRoutineEnum::kHttpFirewall, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunHttpsFirewallRoutine(
    RunHttpsFirewallRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeHttpsFirewallRoutine(),
             mojom::DiagnosticRoutineEnum::kHttpsFirewall, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunHttpsLatencyRoutine(
    RunHttpsLatencyRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeHttpsLatencyRoutine(),
             mojom::DiagnosticRoutineEnum::kHttpsLatency, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunLanConnectivityRoutine(
    RunLanConnectivityRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeLanConnectivityRoutine(),
             mojom::DiagnosticRoutineEnum::kLanConnectivity,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunMemoryRoutine(
    std::optional<uint32_t> max_testing_mem_kib,
    RunMemoryRoutineCallback callback) {
  auto args = mojom::RoutineArgument::NewMemory(
      mojom::MemoryRoutineArgument::New(max_testing_mem_kib));
  RunRoutineWithAdapter(std::move(args), mojom::DiagnosticRoutineEnum::kMemory,
                        std::move(callback));
}

void CrosHealthdDiagnosticsService::RunNvmeSelfTestRoutine(
    mojom::NvmeSelfTestTypeEnum nvme_self_test_type,
    RunNvmeSelfTestRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeNvmeSelfTestRoutine(context_->debugd_proxy(),
                                                       nvme_self_test_type),
             mojom::DiagnosticRoutineEnum::kNvmeSelfTest, std::move(callback));
}

void CrosHealthdDiagnosticsService::DEPRECATED_RunNvmeWearLevelRoutine(
    uint32_t wear_level_threshold, RunNvmeWearLevelRoutineCallback callback) {
  RunRoutine(
      routine_factory_->MakeNvmeWearLevelRoutine(
          context_->debugd_proxy(),
          ash::cros_healthd::mojom::NullableUint32::New(wear_level_threshold)),
      mojom::DiagnosticRoutineEnum::kNvmeWearLevel, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunNvmeWearLevelRoutine(
    ash::cros_healthd::mojom::NullableUint32Ptr wear_level_threshold,
    RunNvmeWearLevelRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeNvmeWearLevelRoutine(
                 context_->debugd_proxy(), std::move(wear_level_threshold)),
             mojom::DiagnosticRoutineEnum::kNvmeWearLevel, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunPrimeSearchRoutine(
    mojom::NullableUint32Ptr length_seconds,
    RunPrimeSearchRoutineCallback callback) {
  std::optional<base::TimeDelta> exec_duration;
  if (!length_seconds.is_null()) {
    exec_duration = base::Seconds(length_seconds->value);
  }

  auto args = mojom::RoutineArgument::NewPrimeSearch(
      mojom::PrimeSearchRoutineArgument::New(exec_duration));
  RunRoutineWithAdapter(std::move(args),
                        mojom::DiagnosticRoutineEnum::kPrimeSearch,
                        std::move(callback));
}

void CrosHealthdDiagnosticsService::RunSignalStrengthRoutine(
    RunSignalStrengthRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeSignalStrengthRoutine(),
             mojom::DiagnosticRoutineEnum::kSignalStrength,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunSmartctlCheckRoutine(
    mojom::NullableUint32Ptr percentage_used_threshold,
    RunSmartctlCheckRoutineCallback callback) {
  RunRoutine(
      routine_factory_->MakeSmartctlCheckRoutine(
          context_->debugd_proxy(), std::move(percentage_used_threshold)),
      mojom::DiagnosticRoutineEnum::kSmartctlCheck, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunUrandomRoutine(
    mojom::NullableUint32Ptr length_seconds,
    RunUrandomRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeUrandomRoutine(std::move(length_seconds)),
             mojom::DiagnosticRoutineEnum::kUrandom, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunVideoConferencingRoutine(
    const std::optional<std::string>& stun_server_hostname,
    RunVideoConferencingRoutineCallback callback) {
  RunRoutine(
      routine_factory_->MakeVideoConferencingRoutine(stun_server_hostname),
      mojom::DiagnosticRoutineEnum::kVideoConferencing, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunArcHttpRoutine(
    RunArcHttpRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeArcHttpRoutine(),
             mojom::DiagnosticRoutineEnum::kArcHttp, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunArcPingRoutine(
    RunArcPingRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeArcPingRoutine(),
             mojom::DiagnosticRoutineEnum::kArcPing, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunArcDnsResolutionRoutine(
    RunArcDnsResolutionRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeArcDnsResolutionRoutine(),
             mojom::DiagnosticRoutineEnum::kArcDnsResolution,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunSensitiveSensorRoutine(
    RunSensitiveSensorRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeSensitiveSensorRoutine(),
             mojom::DiagnosticRoutineEnum::kSensitiveSensor,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunFingerprintRoutine(
    RunFingerprintRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeFingerprintRoutine(),
             mojom::DiagnosticRoutineEnum::kFingerprint, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunFingerprintAliveRoutine(
    RunFingerprintAliveRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeFingerprintAliveRoutine(),
             mojom::DiagnosticRoutineEnum::kFingerprintAlive,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunPrivacyScreenRoutine(
    bool target_state, RunPrivacyScreenRoutineCallback callback) {
  RunRoutine(routine_factory_->MakePrivacyScreenRoutine(target_state),
             mojom::DiagnosticRoutineEnum::kPrivacyScreen, std::move(callback));
}

void CrosHealthdDiagnosticsService::DEPRECATED_RunLedLitUpRoutine(
    mojom::DEPRECATED_LedName name,
    mojom::DEPRECATED_LedColor color,
    mojo::PendingRemote<mojom::DEPRECATED_LedLitUpRoutineReplier> replier,
    DEPRECATED_RunLedLitUpRoutineCallback callback) {
  // Always unsupported. The routine is deprecated.
  ReportUnsupportedRoutine(mojom::DiagnosticRoutineEnum::kLedLitUp,
                           std::move(callback));
}

void CrosHealthdDiagnosticsService::RunEmmcLifetimeRoutine(
    RunEmmcLifetimeRoutineCallback callback) {
  RunRoutine(
      routine_factory_->MakeEmmcLifetimeRoutine(context_->debugd_proxy()),
      mojom::DiagnosticRoutineEnum::kEmmcLifetime, std::move(callback));
}

void CrosHealthdDiagnosticsService::DEPRECATED_RunAudioSetVolumeRoutine(
    uint64_t node_id,
    uint8_t volume,
    bool mute_on,
    DEPRECATED_RunAudioSetVolumeRoutineCallback callback) {
  // Always unsupported. The routine is deprecated.
  ReportUnsupportedRoutine(
      mojom::DiagnosticRoutineEnum::DEPRECATED_kAudioSetVolume,
      std::move(callback));
}

void CrosHealthdDiagnosticsService::DEPRECATED_RunAudioSetGainRoutine(
    uint64_t node_id,
    uint8_t gain,
    bool deprecated_mute_on,
    DEPRECATED_RunAudioSetGainRoutineCallback callback) {
  // Always unsupported. The routine is deprecated.
  ReportUnsupportedRoutine(
      mojom::DiagnosticRoutineEnum::DEPRECATED_kAudioSetGain,
      std::move(callback));
}

void CrosHealthdDiagnosticsService::RunBluetoothPowerRoutine(
    RunBluetoothPowerRoutineCallback callback) {
  CheckFlossEnabled(
      context_->floss_controller(),
      base::BindOnce(&CrosHealthdDiagnosticsService::
                         RunBluetoothPowerRoutineBasedOnFlossEnabled,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void CrosHealthdDiagnosticsService::RunBluetoothPowerRoutineBasedOnFlossEnabled(
    RunBluetoothPowerRoutineCallback callback,
    brillo::Error* err,
    bool floss_enabled) {
  if (!err && floss_enabled) {
    auto args = mojom::RoutineArgument::NewBluetoothPower(
        mojom::BluetoothPowerRoutineArgument::New());
    RunRoutineWithAdapter(std::move(args),
                          mojom::DiagnosticRoutineEnum::kBluetoothPower,
                          std::move(callback));
    return;
  }
  // Fall back to using Bluez to run Bluetooth routine.
  RunRoutine(routine_factory_->MakeBluetoothPowerRoutine(),
             mojom::DiagnosticRoutineEnum::kBluetoothPower,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunBluetoothDiscoveryRoutine(
    RunBluetoothDiscoveryRoutineCallback callback) {
  CheckFlossEnabled(
      context_->floss_controller(),
      base::BindOnce(&CrosHealthdDiagnosticsService::
                         RunBluetoothDiscoveryRoutineBasedOnFlossEnabled,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void CrosHealthdDiagnosticsService::
    RunBluetoothDiscoveryRoutineBasedOnFlossEnabled(
        RunBluetoothDiscoveryRoutineCallback callback,
        brillo::Error* err,
        bool floss_enabled) {
  if (!err && floss_enabled) {
    auto args = mojom::RoutineArgument::NewBluetoothDiscovery(
        mojom::BluetoothDiscoveryRoutineArgument::New());
    RunRoutineWithAdapter(std::move(args),
                          mojom::DiagnosticRoutineEnum::kBluetoothDiscovery,
                          std::move(callback));
    return;
  }
  // Fall back to using Bluez to run Bluetooth routine.
  RunRoutine(routine_factory_->MakeBluetoothDiscoveryRoutine(),
             mojom::DiagnosticRoutineEnum::kBluetoothDiscovery,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunBluetoothScanningRoutine(
    mojom::NullableUint32Ptr length_seconds,
    RunBluetoothScanningRoutineCallback callback) {
  CheckFlossEnabled(
      context_->floss_controller(),
      base::BindOnce(&CrosHealthdDiagnosticsService::
                         RunBluetoothScanningRoutineBasedOnFlossEnabled,
                     weak_ptr_factory_.GetWeakPtr(), std::move(length_seconds),
                     std::move(callback)));
}

void CrosHealthdDiagnosticsService::
    RunBluetoothScanningRoutineBasedOnFlossEnabled(
        ash::cros_healthd::mojom::NullableUint32Ptr length_seconds,
        RunBluetoothScanningRoutineCallback callback,
        brillo::Error* err,
        bool floss_enabled) {
  std::optional<base::TimeDelta> exec_duration;
  if (!length_seconds.is_null())
    exec_duration = base::Seconds(length_seconds->value);

  if (!err && floss_enabled) {
    auto args = mojom::BluetoothScanningRoutineArgument::New();
    args->exec_duration = exec_duration;
    RunRoutineWithAdapter(
        mojom::RoutineArgument::NewBluetoothScanning(std::move(args)),
        mojom::DiagnosticRoutineEnum::kBluetoothScanning, std::move(callback));
    return;
  }
  // Fall back to using Bluez to run Bluetooth routine.
  RunRoutine(routine_factory_->MakeBluetoothScanningRoutine(exec_duration),
             mojom::DiagnosticRoutineEnum::kBluetoothScanning,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunBluetoothPairingRoutine(
    const std::string& peripheral_id,
    RunBluetoothPairingRoutineCallback callback) {
  CheckFlossEnabled(
      context_->floss_controller(),
      base::BindOnce(&CrosHealthdDiagnosticsService::
                         RunBluetoothPairingRoutineBasedOnFlossEnabled,
                     weak_ptr_factory_.GetWeakPtr(), peripheral_id,
                     std::move(callback)));
}

void CrosHealthdDiagnosticsService::
    RunBluetoothPairingRoutineBasedOnFlossEnabled(
        const std::string& peripheral_id,
        RunBluetoothPairingRoutineCallback callback,
        brillo::Error* err,
        bool floss_enabled) {
  if (!err && floss_enabled) {
    auto args = mojom::BluetoothPairingRoutineArgument::New();
    args->peripheral_id = peripheral_id;
    RunRoutineWithAdapter(
        mojom::RoutineArgument::NewBluetoothPairing(std::move(args)),
        mojom::DiagnosticRoutineEnum::kBluetoothPairing, std::move(callback));
    return;
  }
  // Fall back to using Bluez to run Bluetooth routine.
  RunRoutine(routine_factory_->MakeBluetoothPairingRoutine(peripheral_id),
             mojom::DiagnosticRoutineEnum::kBluetoothPairing,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunPowerButtonRoutine(
    uint32_t timeout_seconds, RunPowerButtonRoutineCallback callback) {
  RunRoutine(routine_factory_->MakePowerButtonRoutine(timeout_seconds),
             mojom::DiagnosticRoutineEnum::kPowerButton, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunAudioDriverRoutine(
    RunAudioDriverRoutineCallback callback) {
  auto args = mojom::RoutineArgument::NewAudioDriver(
      mojom::AudioDriverRoutineArgument::New());
  RunRoutineWithAdapter(std::move(args),
                        mojom::DiagnosticRoutineEnum::kAudioDriver,
                        std::move(callback));
}

void CrosHealthdDiagnosticsService::RunUfsLifetimeRoutine(
    RunUfsLifetimeRoutineCallback callback) {
  auto args = mojom::RoutineArgument::NewUfsLifetime(
      mojom::UfsLifetimeRoutineArgument::New());
  RunRoutineWithAdapter(std::move(args),
                        mojom::DiagnosticRoutineEnum::kUfsLifetime,
                        std::move(callback));
}

void CrosHealthdDiagnosticsService::RunFanRoutine(
    RunFanRoutineCallback callback) {
  auto args = mojom::RoutineArgument::NewFan(mojom::FanRoutineArgument::New());
  RunRoutineWithAdapter(std::move(args), mojom::DiagnosticRoutineEnum::kFan,
                        std::move(callback));
}

void CrosHealthdDiagnosticsService::RunRoutine(
    std::unique_ptr<DiagnosticRoutine> routine,
    mojom::DiagnosticRoutineEnum routine_enum,
    base::OnceCallback<void(mojom::RunRoutineResponsePtr)> callback) {
  DCHECK(routine);

  if (!available_routines_.contains(routine_enum)) {
    ReportUnsupportedRoutine(routine_enum, std::move(callback));
    return;
  }

  CHECK(next_id_ < std::numeric_limits<int32_t>::max())
      << "Maximum number of routines exceeded.";

  // Send the result to UMA once the routine enters a terminal status.
  routine->RegisterStatusChangedCallback(
      InvokeOnTerminalStatus(base::BindOnce(&SendResultToUMA, routine_enum)));

  routine->Start();
  int32_t id = next_id_;
  CHECK(!active_routines_.contains(id));
  active_routines_[id] = std::move(routine);
  ++next_id_;

  std::move(callback).Run(
      mojom::RunRoutineResponse::New(id, active_routines_[id]->GetStatus()));
}

void CrosHealthdDiagnosticsService::RunRoutineWithAdapter(
    ash::cros_healthd::mojom::RoutineArgumentPtr argument,
    mojom::DiagnosticRoutineEnum routine_enum,
    base::OnceCallback<void(mojom::RunRoutineResponsePtr)> callback) {
  auto routine_v2 = std::make_unique<RoutineAdapter>(argument->which());
  routine_v2->SetupAdapter(std::move(argument), routine_service_);

  RunRoutine(std::move(routine_v2), routine_enum, std::move(callback));
}

void CrosHealthdDiagnosticsService::HandleNvmeSelfTestSupportedResponse(
    bool supported) {
  if (supported) {
    available_routines_.insert(mojom::DiagnosticRoutineEnum::kNvmeSelfTest);
  }
}

void CrosHealthdDiagnosticsService::OnServiceReady() {
  LOG(INFO) << "CrosHealthdDiagnosticsService is ready.";
  provider_.Register(context_->mojo_service()->GetServiceManager(),
                     chromeos::mojo_services::kCrosHealthdDiagnostics);
}

void CrosHealthdDiagnosticsService::PopulateAvailableRoutines(
    base::OnceClosure completion_callback) {
  // |barreir| will be destructed automatically at the end of this function,
  // which ensures |completion_callback| will only be run after all the
  // synchronous and asynchronous availability checks are done.
  CallbackBarrier barreir{
      base::IgnoreArgs<bool>(std::move(completion_callback))};

  // Routines that are supported on all devices.
  available_routines_ = {
      mojom::DiagnosticRoutineEnum::kUrandom,
      mojom::DiagnosticRoutineEnum::kAcPower,
      mojom::DiagnosticRoutineEnum::kCpuCache,
      mojom::DiagnosticRoutineEnum::kCpuStress,
      mojom::DiagnosticRoutineEnum::kFloatingPointAccuracy,
      mojom::DiagnosticRoutineEnum::kPrimeSearch,
      mojom::DiagnosticRoutineEnum::kDiskRead,
      mojom::DiagnosticRoutineEnum::kMemory,
      mojom::DiagnosticRoutineEnum::kLanConnectivity,
      mojom::DiagnosticRoutineEnum::kSignalStrength,
      mojom::DiagnosticRoutineEnum::kGatewayCanBePinged,
      mojom::DiagnosticRoutineEnum::kHasSecureWiFiConnection,
      mojom::DiagnosticRoutineEnum::kDnsResolverPresent,
      mojom::DiagnosticRoutineEnum::kDnsLatency,
      mojom::DiagnosticRoutineEnum::kDnsResolution,
      mojom::DiagnosticRoutineEnum::kCaptivePortal,
      mojom::DiagnosticRoutineEnum::kHttpFirewall,
      mojom::DiagnosticRoutineEnum::kHttpsFirewall,
      mojom::DiagnosticRoutineEnum::kHttpsLatency,
      mojom::DiagnosticRoutineEnum::kVideoConferencing,
      mojom::DiagnosticRoutineEnum::kArcHttp,
      mojom::DiagnosticRoutineEnum::kArcPing,
      mojom::DiagnosticRoutineEnum::kArcDnsResolution,
      mojom::DiagnosticRoutineEnum::kSensitiveSensor,
      mojom::DiagnosticRoutineEnum::kBluetoothPower,
      mojom::DiagnosticRoutineEnum::kBluetoothDiscovery,
      mojom::DiagnosticRoutineEnum::kBluetoothScanning,
      mojom::DiagnosticRoutineEnum::kBluetoothPairing,
      mojom::DiagnosticRoutineEnum::kPowerButton,
      mojom::DiagnosticRoutineEnum::kAudioDriver,
      mojom::DiagnosticRoutineEnum::kFan,
  };

  if (context_->system_config()->HasBattery()) {
    available_routines_.insert(mojom::DiagnosticRoutineEnum::kBatteryCapacity);
    available_routines_.insert(mojom::DiagnosticRoutineEnum::kBatteryHealth);
    available_routines_.insert(mojom::DiagnosticRoutineEnum::kBatteryDischarge);
    available_routines_.insert(mojom::DiagnosticRoutineEnum::kBatteryCharge);
  }

  if (context_->system_config()->NvmeSupported()) {
    if (context_->system_config()->IsWilcoDevice()) {
      available_routines_.insert(mojom::DiagnosticRoutineEnum::kNvmeWearLevel);
    }
    if (context_->system_config()->SmartCtlSupported()) {
      available_routines_.insert(mojom::DiagnosticRoutineEnum::kSmartctlCheck);
      available_routines_.insert(
          mojom::DiagnosticRoutineEnum::kSmartctlCheckWithPercentageUsed);
    }
    auto nvme_self_test_supported_callback = base::BindOnce(
        &CrosHealthdDiagnosticsService::HandleNvmeSelfTestSupportedResponse,
        weak_ptr_factory_.GetWeakPtr());
    context_->system_config()->NvmeSelfTestSupported(
        barreir.Depend(std::move(nvme_self_test_supported_callback)));
  }

  if (context_->system_config()->FingerprintDiagnosticSupported()) {
    available_routines_.insert(mojom::DiagnosticRoutineEnum::kFingerprint);
    available_routines_.insert(mojom::DiagnosticRoutineEnum::kFingerprintAlive);
  }

  if (context_->system_config()->HasPrivacyScreen()) {
    available_routines_.insert(mojom::DiagnosticRoutineEnum::kPrivacyScreen);
  }

  if (context_->system_config()->MmcSupported()) {
    available_routines_.insert(mojom::DiagnosticRoutineEnum::kEmmcLifetime);
  }

  if (ground_truth_->PrepareRoutineUfsLifetime()->is_supported()) {
    available_routines_.insert(mojom::DiagnosticRoutineEnum::kUfsLifetime);
  }
}

}  // namespace diagnostics
