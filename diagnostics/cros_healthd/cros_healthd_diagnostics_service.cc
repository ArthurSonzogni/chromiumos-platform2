// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/cros_healthd_diagnostics_service.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/time/time.h>
#include <chromeos/mojo/service_constants.h>
#include <metrics/metrics_library.h>

#include "diagnostics/cros_healthd/system/system_config.h"
#include "diagnostics/cros_healthd/utils/callback_barrier.h"
#include "diagnostics/cros_healthd/utils/metrics_utils.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"
#include "diagnostics/mojom/public/nullable_primitives.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::ash::cros_healthd::mojom;

using OnTerminalStatusCallback =
    base::OnceCallback<void(mojo_ipc::DiagnosticRoutineStatusEnum)>;

void SetErrorRoutineUpdate(const std::string& status_message,
                           mojo_ipc::RoutineUpdate* response) {
  auto noninteractive_update = mojo_ipc::NonInteractiveRoutineUpdate::New();
  noninteractive_update->status = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
  noninteractive_update->status_message = status_message;
  response->routine_update_union =
      mojo_ipc::RoutineUpdateUnion::NewNoninteractiveUpdate(
          std::move(noninteractive_update));
  response->progress_percent = 0;
}

// Wrap |on_terminal_status_callback| as a StatusChangedCallback that invokes
// |on_terminal_status_callback| with the first terminal routine status it
// receives.
//
// Terminal status mean these enums
// - kPassed
// - kFailed
// - kError
// - kCancelled
// - kFailedToStart
// - kRemoved
// - kUnsupported
// - kNotRun
DiagnosticRoutine::StatusChangedCallback
WrapOnTerminalStatusCallbackAsStatusChangedCallback(
    OnTerminalStatusCallback on_terminal_status_callback) {
  return base::BindRepeating(
      [](OnTerminalStatusCallback& on_terminal_status,
         mojo_ipc::DiagnosticRoutineStatusEnum status) {
        switch (status) {
          // Non-terminal status.
          case mojo_ipc::DiagnosticRoutineStatusEnum::kUnknown:
          case mojo_ipc::DiagnosticRoutineStatusEnum::kReady:
          case mojo_ipc::DiagnosticRoutineStatusEnum::kRunning:
          case mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting:
          case mojo_ipc::DiagnosticRoutineStatusEnum::kCancelling: {
            break;
          }
          // Terminal status.
          case mojo_ipc::DiagnosticRoutineStatusEnum::kPassed:
          case mojo_ipc::DiagnosticRoutineStatusEnum::kFailed:
          case mojo_ipc::DiagnosticRoutineStatusEnum::kError:
          case mojo_ipc::DiagnosticRoutineStatusEnum::kCancelled:
          case mojo_ipc::DiagnosticRoutineStatusEnum::kFailedToStart:
          case mojo_ipc::DiagnosticRoutineStatusEnum::kRemoved:
          case mojo_ipc::DiagnosticRoutineStatusEnum::kUnsupported:
          case mojo_ipc::DiagnosticRoutineStatusEnum::kNotRun: {
            // |on_terminal_status| will be null if it has already been called.
            if (on_terminal_status) {
              std::move(on_terminal_status).Run(status);
            }
            break;
          }
        }
      },
      base::OwnedRef(std::move(on_terminal_status_callback)));
}

void SendResultToUMA(mojo_ipc::DiagnosticRoutineEnum routine,
                     mojo_ipc::DiagnosticRoutineStatusEnum status) {
  MetricsLibrary metrics;
  SendDiagnosticResultToUMA(&metrics, routine, status);
}

}  // namespace

CrosHealthdDiagnosticsService::CrosHealthdDiagnosticsService(
    Context* context, CrosHealthdRoutineFactory* routine_factory)
    : context_(context), routine_factory_(routine_factory), provider_(this) {
  DCHECK(context_);
  DCHECK(routine_factory_);

  // Service is ready after available routines are populated.
  PopulateAvailableRoutines(
      base::BindOnce(&CrosHealthdDiagnosticsService::OnServiceReady,
                     weak_ptr_factory_.GetWeakPtr()));
}

CrosHealthdDiagnosticsService::~CrosHealthdDiagnosticsService() = default;

void CrosHealthdDiagnosticsService::RegisterServiceReadyCallback(
    base::OnceClosure callback) {
  if (ready_) {
    std::move(callback).Run();
  } else {
    service_ready_callbacks_.push_back(std::move(callback));
  }
}

void CrosHealthdDiagnosticsService::GetAvailableRoutines(
    GetAvailableRoutinesCallback callback) {
  std::move(callback).Run(std::vector<mojo_ipc::DiagnosticRoutineEnum>(
      available_routines_.begin(), available_routines_.end()));
}

void CrosHealthdDiagnosticsService::GetRoutineUpdate(
    int32_t id,
    mojo_ipc::DiagnosticRoutineCommandEnum command,
    bool include_output,
    GetRoutineUpdateCallback callback) {
  mojo_ipc::RoutineUpdate update{0, mojo::ScopedHandle(),
                                 mojo_ipc::RoutineUpdateUnionPtr()};

  auto itr = active_routines_.find(id);
  if (itr == active_routines_.end()) {
    LOG(ERROR) << "Bad id in GetRoutineUpdateRequest: " << id;
    SetErrorRoutineUpdate("Specified routine does not exist.", &update);
    std::move(callback).Run(mojo_ipc::RoutineUpdate::New(
        update.progress_percent, std::move(update.output),
        std::move(update.routine_update_union)));
    return;
  }

  auto* routine = itr->second.get();
  switch (command) {
    case mojo_ipc::DiagnosticRoutineCommandEnum::kContinue:
      routine->Resume();
      break;
    case mojo_ipc::DiagnosticRoutineCommandEnum::kCancel:
      routine->Cancel();
      break;
    case mojo_ipc::DiagnosticRoutineCommandEnum::kGetStatus:
      // Retrieving the status and output of a routine is handled below.
      break;
    case mojo_ipc::DiagnosticRoutineCommandEnum::kRemove:
      routine->PopulateStatusUpdate(&update, include_output);
      if (update.routine_update_union->is_noninteractive_update()) {
        update.routine_update_union->get_noninteractive_update()->status =
            mojo_ipc::DiagnosticRoutineStatusEnum::kRemoved;
      }
      active_routines_.erase(itr);
      // |routine| is invalid at this point!
      std::move(callback).Run(mojo_ipc::RoutineUpdate::New(
          update.progress_percent, std::move(update.output),
          std::move(update.routine_update_union)));
      return;
    case mojo_ipc::DiagnosticRoutineCommandEnum::kUnknown:
      LOG(ERROR) << "Get unknown command";
      break;
  }

  routine->PopulateStatusUpdate(&update, include_output);
  std::move(callback).Run(mojo_ipc::RoutineUpdate::New(
      update.progress_percent, std::move(update.output),
      std::move(update.routine_update_union)));
}

void CrosHealthdDiagnosticsService::RunAcPowerRoutine(
    mojo_ipc::AcPowerStatusEnum expected_status,
    const std::optional<std::string>& expected_power_type,
    RunAcPowerRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeAcPowerRoutine(expected_status,
                                                  expected_power_type),
             mojo_ipc::DiagnosticRoutineEnum::kAcPower, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunBatteryCapacityRoutine(
    RunBatteryCapacityRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeBatteryCapacityRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kBatteryCapacity,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunBatteryChargeRoutine(
    uint32_t length_seconds,
    uint32_t minimum_charge_percent_required,
    RunBatteryChargeRoutineCallback callback) {
  RunRoutine(
      routine_factory_->MakeBatteryChargeRoutine(
          base::Seconds(length_seconds), minimum_charge_percent_required),
      mojo_ipc::DiagnosticRoutineEnum::kBatteryCharge, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunBatteryDischargeRoutine(
    uint32_t length_seconds,
    uint32_t maximum_discharge_percent_allowed,
    RunBatteryDischargeRoutineCallback callback) {
  RunRoutine(
      routine_factory_->MakeBatteryDischargeRoutine(
          base::Seconds(length_seconds), maximum_discharge_percent_allowed),
      mojo_ipc::DiagnosticRoutineEnum::kBatteryDischarge, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunBatteryHealthRoutine(
    RunBatteryHealthRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeBatteryHealthRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kBatteryHealth,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunCaptivePortalRoutine(
    RunCaptivePortalRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeCaptivePortalRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kCaptivePortal,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunCpuCacheRoutine(
    mojo_ipc::NullableUint32Ptr length_seconds,
    RunCpuCacheRoutineCallback callback) {
  std::optional<base::TimeDelta> exec_duration;
  if (!length_seconds.is_null())
    exec_duration = base::Seconds(length_seconds->value);
  RunRoutine(routine_factory_->MakeCpuCacheRoutine(exec_duration),
             mojo_ipc::DiagnosticRoutineEnum::kCpuCache, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunCpuStressRoutine(
    mojo_ipc::NullableUint32Ptr length_seconds,
    RunCpuStressRoutineCallback callback) {
  std::optional<base::TimeDelta> exec_duration;
  if (!length_seconds.is_null())
    exec_duration = base::Seconds(length_seconds->value);
  RunRoutine(routine_factory_->MakeCpuStressRoutine(exec_duration),
             mojo_ipc::DiagnosticRoutineEnum::kCpuStress, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunDiskReadRoutine(
    mojo_ipc::DiskReadRoutineTypeEnum type,
    uint32_t length_seconds,
    uint32_t file_size_mb,
    RunDiskReadRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeDiskReadRoutine(
                 type, base::Seconds(length_seconds), file_size_mb),
             mojo_ipc::DiagnosticRoutineEnum::kDiskRead, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunDnsLatencyRoutine(
    RunDnsLatencyRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeDnsLatencyRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kDnsLatency, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunDnsResolutionRoutine(
    RunDnsResolutionRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeDnsResolutionRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kDnsResolution,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunDnsResolverPresentRoutine(
    RunDnsResolverPresentRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeDnsResolverPresentRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kDnsResolverPresent,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunFloatingPointAccuracyRoutine(
    mojo_ipc::NullableUint32Ptr length_seconds,
    RunFloatingPointAccuracyRoutineCallback callback) {
  std::optional<base::TimeDelta> exec_duration;
  if (!length_seconds.is_null())
    exec_duration = base::Seconds(length_seconds->value);
  RunRoutine(routine_factory_->MakeFloatingPointAccuracyRoutine(exec_duration),
             mojo_ipc::DiagnosticRoutineEnum::kFloatingPointAccuracy,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunGatewayCanBePingedRoutine(
    RunGatewayCanBePingedRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeGatewayCanBePingedRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kGatewayCanBePinged,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunHasSecureWiFiConnectionRoutine(
    RunHasSecureWiFiConnectionRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeHasSecureWiFiConnectionRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kHasSecureWiFiConnection,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunHttpFirewallRoutine(
    RunHttpFirewallRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeHttpFirewallRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kHttpFirewall,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunHttpsFirewallRoutine(
    RunHttpsFirewallRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeHttpsFirewallRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kHttpsFirewall,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunHttpsLatencyRoutine(
    RunHttpsLatencyRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeHttpsLatencyRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kHttpsLatency,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunLanConnectivityRoutine(
    RunLanConnectivityRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeLanConnectivityRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kLanConnectivity,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunMemoryRoutine(
    RunMemoryRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeMemoryRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kMemory, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunNvmeSelfTestRoutine(
    mojo_ipc::NvmeSelfTestTypeEnum nvme_self_test_type,
    RunNvmeSelfTestRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeNvmeSelfTestRoutine(context_->debugd_proxy(),
                                                       nvme_self_test_type),
             mojo_ipc::DiagnosticRoutineEnum::kNvmeSelfTest,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::DEPRECATED_RunNvmeWearLevelRoutine(
    uint32_t wear_level_threshold, RunNvmeWearLevelRoutineCallback callback) {
  RunRoutine(
      routine_factory_->MakeNvmeWearLevelRoutine(
          context_->debugd_proxy(),
          ash::cros_healthd::mojom::NullableUint32::New(wear_level_threshold)),
      mojo_ipc::DiagnosticRoutineEnum::kNvmeWearLevel, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunNvmeWearLevelRoutine(
    ash::cros_healthd::mojom::NullableUint32Ptr wear_level_threshold,
    RunNvmeWearLevelRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeNvmeWearLevelRoutine(
                 context_->debugd_proxy(), std::move(wear_level_threshold)),
             mojo_ipc::DiagnosticRoutineEnum::kNvmeWearLevel,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunPrimeSearchRoutine(
    mojo_ipc::NullableUint32Ptr length_seconds,
    RunPrimeSearchRoutineCallback callback) {
  std::optional<base::TimeDelta> exec_duration;
  if (!length_seconds.is_null())
    exec_duration = base::Seconds(length_seconds->value);
  RunRoutine(routine_factory_->MakePrimeSearchRoutine(exec_duration),
             mojo_ipc::DiagnosticRoutineEnum::kPrimeSearch,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunSignalStrengthRoutine(
    RunSignalStrengthRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeSignalStrengthRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kSignalStrength,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunSmartctlCheckRoutine(
    mojo_ipc::NullableUint32Ptr percentage_used_threshold,
    RunSmartctlCheckRoutineCallback callback) {
  RunRoutine(
      routine_factory_->MakeSmartctlCheckRoutine(
          context_->debugd_proxy(), std::move(percentage_used_threshold)),
      mojo_ipc::DiagnosticRoutineEnum::kSmartctlCheck, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunUrandomRoutine(
    mojo_ipc::NullableUint32Ptr length_seconds,
    RunUrandomRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeUrandomRoutine(std::move(length_seconds)),
             mojo_ipc::DiagnosticRoutineEnum::kUrandom, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunVideoConferencingRoutine(
    const std::optional<std::string>& stun_server_hostname,
    RunVideoConferencingRoutineCallback callback) {
  RunRoutine(
      routine_factory_->MakeVideoConferencingRoutine(stun_server_hostname),
      mojo_ipc::DiagnosticRoutineEnum::kVideoConferencing, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunArcHttpRoutine(
    RunArcHttpRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeArcHttpRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kArcHttp, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunArcPingRoutine(
    RunArcPingRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeArcPingRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kArcPing, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunArcDnsResolutionRoutine(
    RunArcDnsResolutionRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeArcDnsResolutionRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kArcDnsResolution,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunSensitiveSensorRoutine(
    RunSensitiveSensorRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeSensitiveSensorRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kSensitiveSensor,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunFingerprintRoutine(
    RunFingerprintRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeFingerprintRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kFingerprint,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunFingerprintAliveRoutine(
    RunFingerprintAliveRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeFingerprintAliveRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kFingerprintAlive,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunPrivacyScreenRoutine(
    bool target_state, RunPrivacyScreenRoutineCallback callback) {
  RunRoutine(routine_factory_->MakePrivacyScreenRoutine(target_state),
             mojo_ipc::DiagnosticRoutineEnum::kPrivacyScreen,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunLedLitUpRoutine(
    mojo_ipc::LedName name,
    mojo_ipc::LedColor color,
    mojo::PendingRemote<mojo_ipc::LedLitUpRoutineReplier> replier,
    RunLedLitUpRoutineCallback callback) {
  RunRoutine(
      routine_factory_->MakeLedLitUpRoutine(name, color, std::move(replier)),
      mojo_ipc::DiagnosticRoutineEnum::kLedLitUp, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunEmmcLifetimeRoutine(
    RunEmmcLifetimeRoutineCallback callback) {
  RunRoutine(
      routine_factory_->MakeEmmcLifetimeRoutine(context_->debugd_proxy()),
      mojo_ipc::DiagnosticRoutineEnum::kEmmcLifetime, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunAudioSetVolumeRoutine(
    uint64_t node_id,
    uint8_t volume,
    bool mute_on,
    RunAudioSetVolumeRoutineCallback callback) {
  RunRoutine(
      routine_factory_->MakeAudioSetVolumeRoutine(node_id, volume, mute_on),
      mojo_ipc::DiagnosticRoutineEnum::kAudioSetVolume, std::move(callback));
}

void CrosHealthdDiagnosticsService::RunAudioSetGainRoutine(
    uint64_t node_id,
    uint8_t gain,
    bool mute_on,
    RunAudioSetGainRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeAudioSetGainRoutine(node_id, gain, mute_on),
             mojo_ipc::DiagnosticRoutineEnum::kAudioSetGain,
             std::move(callback));
}

void CrosHealthdDiagnosticsService::RunRoutine(
    std::unique_ptr<DiagnosticRoutine> routine,
    mojo_ipc::DiagnosticRoutineEnum routine_enum,
    base::OnceCallback<void(mojo_ipc::RunRoutineResponsePtr)> callback) {
  DCHECK(routine);

  if (!available_routines_.count(routine_enum)) {
    LOG(ERROR) << routine_enum << " is not supported on this device";
    SendResultToUMA(routine_enum,
                    mojo_ipc::DiagnosticRoutineStatusEnum::kUnsupported);
    std::move(callback).Run(mojo_ipc::RunRoutineResponse::New(
        mojo_ipc::kFailedToStartId,
        mojo_ipc::DiagnosticRoutineStatusEnum::kUnsupported));
    return;
  }

  CHECK(next_id_ < std::numeric_limits<int32_t>::max())
      << "Maximum number of routines exceeded.";

  // Send the result to UMA once the routine enters a terminal status.
  routine->RegisterStatusChangedCallback(
      WrapOnTerminalStatusCallbackAsStatusChangedCallback(
          base::BindOnce(&SendResultToUMA, routine_enum)));

  routine->Start();
  int32_t id = next_id_;
  DCHECK(active_routines_.find(id) == active_routines_.end());
  active_routines_[id] = std::move(routine);
  ++next_id_;

  std::move(callback).Run(
      mojo_ipc::RunRoutineResponse::New(id, active_routines_[id]->GetStatus()));
}

void CrosHealthdDiagnosticsService::HandleNvmeSelfTestSupportedResponse(
    bool supported) {
  if (supported) {
    available_routines_.insert(mojo_ipc::DiagnosticRoutineEnum::kNvmeSelfTest);
  }
}

void CrosHealthdDiagnosticsService::OnServiceReady() {
  LOG(INFO) << "CrosHealthdDiagnosticsService is ready.";
  ready_ = true;

  provider_.Register(context_->mojo_service()->GetServiceManager(),
                     chromeos::mojo_services::kCrosHealthdDiagnostics);

  // Run all the callbacks.
  std::vector<base::OnceClosure> callbacks;
  callbacks.swap(service_ready_callbacks_);
  for (size_t i = 0; i < callbacks.size(); ++i) {
    std::move(callbacks[i]).Run();
  }
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
      mojo_ipc::DiagnosticRoutineEnum::kUrandom,
      mojo_ipc::DiagnosticRoutineEnum::kAcPower,
      mojo_ipc::DiagnosticRoutineEnum::kCpuCache,
      mojo_ipc::DiagnosticRoutineEnum::kCpuStress,
      mojo_ipc::DiagnosticRoutineEnum::kFloatingPointAccuracy,
      mojo_ipc::DiagnosticRoutineEnum::kPrimeSearch,
      mojo_ipc::DiagnosticRoutineEnum::kMemory,
      mojo_ipc::DiagnosticRoutineEnum::kLanConnectivity,
      mojo_ipc::DiagnosticRoutineEnum::kSignalStrength,
      mojo_ipc::DiagnosticRoutineEnum::kGatewayCanBePinged,
      mojo_ipc::DiagnosticRoutineEnum::kHasSecureWiFiConnection,
      mojo_ipc::DiagnosticRoutineEnum::kDnsResolverPresent,
      mojo_ipc::DiagnosticRoutineEnum::kDnsLatency,
      mojo_ipc::DiagnosticRoutineEnum::kDnsResolution,
      mojo_ipc::DiagnosticRoutineEnum::kCaptivePortal,
      mojo_ipc::DiagnosticRoutineEnum::kHttpFirewall,
      mojo_ipc::DiagnosticRoutineEnum::kHttpsFirewall,
      mojo_ipc::DiagnosticRoutineEnum::kHttpsLatency,
      mojo_ipc::DiagnosticRoutineEnum::kVideoConferencing,
      mojo_ipc::DiagnosticRoutineEnum::kArcHttp,
      mojo_ipc::DiagnosticRoutineEnum::kArcPing,
      mojo_ipc::DiagnosticRoutineEnum::kArcDnsResolution,
      mojo_ipc::DiagnosticRoutineEnum::kSensitiveSensor,
      mojo_ipc::DiagnosticRoutineEnum::kAudioSetVolume,
      mojo_ipc::DiagnosticRoutineEnum::kAudioSetGain,
  };

  if (context_->system_config()->HasBattery()) {
    available_routines_.insert(
        mojo_ipc::DiagnosticRoutineEnum::kBatteryCapacity);
    available_routines_.insert(mojo_ipc::DiagnosticRoutineEnum::kBatteryHealth);
    available_routines_.insert(
        mojo_ipc::DiagnosticRoutineEnum::kBatteryDischarge);
    available_routines_.insert(mojo_ipc::DiagnosticRoutineEnum::kBatteryCharge);
  }

  if (context_->system_config()->NvmeSupported()) {
    if (context_->system_config()->IsWilcoDevice()) {
      available_routines_.insert(
          mojo_ipc::DiagnosticRoutineEnum::kNvmeWearLevel);
    }
    auto nvme_self_test_supported_callback = base::BindOnce(
        &CrosHealthdDiagnosticsService::HandleNvmeSelfTestSupportedResponse,
        weak_ptr_factory_.GetWeakPtr());
    context_->system_config()->NvmeSelfTestSupported(
        barreir.Depend(std::move(nvme_self_test_supported_callback)));
  }

  if (context_->system_config()->SmartCtlSupported()) {
    available_routines_.insert(mojo_ipc::DiagnosticRoutineEnum::kSmartctlCheck);
    available_routines_.insert(
        mojo_ipc::DiagnosticRoutineEnum::kSmartctlCheckWithPercentageUsed);
  }

  if (context_->system_config()->FioSupported()) {
    available_routines_.insert(mojo_ipc::DiagnosticRoutineEnum::kDiskRead);
  }

  if (context_->system_config()->FingerprintDiagnosticSupported()) {
    available_routines_.insert(mojo_ipc::DiagnosticRoutineEnum::kFingerprint);
    available_routines_.insert(
        mojo_ipc::DiagnosticRoutineEnum::kFingerprintAlive);
  }

  if (context_->system_config()->HasPrivacyScreen()) {
    available_routines_.insert(mojo_ipc::DiagnosticRoutineEnum::kPrivacyScreen);
  }

  if (context_->system_config()->MmcSupported()) {
    available_routines_.insert(mojo_ipc::DiagnosticRoutineEnum::kEmmcLifetime);
  }

  if (context_->system_config()->HasChromiumEC()) {
    available_routines_.insert(mojo_ipc::DiagnosticRoutineEnum::kLedLitUp);
  }
}

}  // namespace diagnostics
