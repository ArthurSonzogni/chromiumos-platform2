// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_health_tool/diag/diag_actions.h"

#include <cstdint>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/check_op.h>
#include <base/json/json_reader.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/no_destructor.h>
#include <base/run_loop.h>
#include <base/task/single_thread_task_runner.h>
#include <base/time/time.h>
#include <mojo/service_constants.h>

#include "diagnostics/base/mojo_utils.h"
#include "diagnostics/cros_health_tool/diag/diag_constants.h"
#include "diagnostics/cros_health_tool/mojo_util.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"
#include "diagnostics/mojom/public/nullable_primitives.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ash::cros_healthd::mojom;

const struct {
  const char* readable_status;
  mojom::DiagnosticRoutineStatusEnum status;
} kDiagnosticRoutineReadableStatuses[] = {
    {"Ready", mojom::DiagnosticRoutineStatusEnum::kReady},
    {"Running", mojom::DiagnosticRoutineStatusEnum::kRunning},
    {"Waiting", mojom::DiagnosticRoutineStatusEnum::kWaiting},
    {"Passed", mojom::DiagnosticRoutineStatusEnum::kPassed},
    {"Failed", mojom::DiagnosticRoutineStatusEnum::kFailed},
    {"Error", mojom::DiagnosticRoutineStatusEnum::kError},
    {"Cancelled", mojom::DiagnosticRoutineStatusEnum::kCancelled},
    {"Failed to start", mojom::DiagnosticRoutineStatusEnum::kFailedToStart},
    {"Removed", mojom::DiagnosticRoutineStatusEnum::kRemoved},
    {"Cancelling", mojom::DiagnosticRoutineStatusEnum::kCancelling},
    {"Unsupported", mojom::DiagnosticRoutineStatusEnum::kUnsupported},
    {"Not run", mojom::DiagnosticRoutineStatusEnum::kNotRun}};

std::string GetSwitchFromRoutine(mojom::DiagnosticRoutineEnum routine) {
  static base::NoDestructor<std::map<mojom::DiagnosticRoutineEnum, std::string>>
      diagnostic_routine_to_switch;

  if (diagnostic_routine_to_switch->empty()) {
    for (const auto& item : kDiagnosticRoutineSwitches) {
      diagnostic_routine_to_switch->insert(
          std::make_pair(item.routine, item.switch_name));
    }
  }

  auto routine_itr = diagnostic_routine_to_switch->find(routine);
  LOG_IF(FATAL, routine_itr == diagnostic_routine_to_switch->end())
      << "Invalid routine to switch lookup with routine: " << routine;

  return routine_itr->second;
}

void WaitUntilEnterPressed() {
  std::cout << "Press ENTER to continue." << std::endl;
  std::string dummy;
  std::getline(std::cin, dummy);
}

void HandleGetLedColorMatchedInvocation(
    mojom::LedLitUpRoutineReplier::GetColorMatchedCallback callback) {
  // Print a newline so we don't overwrite the progress percent.
  std::cout << '\n';

  std::optional<bool> answer;
  do {
    std::cout << "Is the LED lit up in the specified color? "
                 "Input y/n then press ENTER to continue."
              << std::endl;
    std::string input;
    std::getline(std::cin, input);

    if (!input.empty() && input[0] == 'y') {
      answer = true;
    } else if (!input.empty() && input[0] == 'n') {
      answer = false;
    }
  } while (!answer.has_value());

  DCHECK(answer.has_value());
  std::move(callback).Run(answer.value());
}

// Saves |response| to |response_destination|.
// TODO(b/262814572): Migrate this to MojoResponseWaiter.
template <class T>
void OnMojoResponseReceived(T* response_destination,
                            base::OnceClosure quit_closure,
                            T response) {
  *response_destination = std::move(response);
  std::move(quit_closure).Run();
}

void PrintStatusMessage(const std::string& status_message) {
  std::cout << "Status message: " << status_message << std::endl;
}

}  // namespace

DiagActions::DiagActions(base::TimeDelta polling_interval,
                         base::TimeDelta maximum_execution_time,
                         const base::TickClock* tick_clock)
    : kPollingInterval(polling_interval),
      kMaximumExecutionTime(maximum_execution_time) {
  // Bind the Diagnostics Service.
  RequestMojoServiceWithDisconnectHandler(
      chromeos::mojo_services::kCrosHealthdDiagnostics,
      cros_healthd_diagnostics_service_);

  if (tick_clock) {
    tick_clock_ = tick_clock;
  } else {
    default_tick_clock_ = std::make_unique<base::DefaultTickClock>();
    tick_clock_ = default_tick_clock_.get();
  }
  DCHECK(tick_clock_);
}

DiagActions::~DiagActions() = default;

mojom::RoutineUpdatePtr DiagActions::GetRoutineUpdate(
    int32_t id,
    mojom::DiagnosticRoutineCommandEnum command,
    bool include_output) {
  mojom::RoutineUpdatePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->GetRoutineUpdate(
      id, command, include_output,
      base::BindOnce(&OnMojoResponseReceived<mojom::RoutineUpdatePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

std::optional<std::vector<mojom::DiagnosticRoutineEnum>>
DiagActions::GetAvailableRoutines() {
  std::vector<mojom::DiagnosticRoutineEnum> response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->GetAvailableRoutines(base::BindOnce(
      [](std::vector<mojom::DiagnosticRoutineEnum>* out,
         base::OnceClosure quit_closure,
         const std::vector<mojom::DiagnosticRoutineEnum>& routines) {
        *out = routines;
        std::move(quit_closure).Run();
      },
      &response, run_loop.QuitClosure()));
  run_loop.Run();
  return response;
}

bool DiagActions::ActionGetRoutines() {
  auto reply = GetAvailableRoutines();
  if (!reply.has_value()) {
    std::cout << "Unable to get available routines from cros_healthd"
              << std::endl;
    return false;
  }

  for (auto routine : reply.value()) {
    std::cout << "Available routine: " << GetSwitchFromRoutine(routine)
              << std::endl;
  }

  return true;
}

bool DiagActions::ActionRunAcPowerRoutine(
    mojom::AcPowerStatusEnum expected_status,
    const std::optional<std::string>& expected_power_type) {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunAcPowerRoutine(
      expected_status, expected_power_type,
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunBatteryCapacityRoutine() {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunBatteryCapacityRoutine(
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunBatteryChargeRoutine(
    base::TimeDelta exec_duration, uint32_t minimum_charge_percent_required) {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunBatteryChargeRoutine(
      exec_duration.InSeconds(), minimum_charge_percent_required,
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunBatteryDischargeRoutine(
    base::TimeDelta exec_duration, uint32_t maximum_discharge_percent_allowed) {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunBatteryDischargeRoutine(
      exec_duration.InSeconds(), maximum_discharge_percent_allowed,
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunBatteryHealthRoutine() {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunBatteryHealthRoutine(
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunCaptivePortalRoutine() {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunCaptivePortalRoutine(
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunCpuCacheRoutine(
    const std::optional<base::TimeDelta>& exec_duration) {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  mojom::NullableUint32Ptr exec_duration_parameter;
  if (exec_duration.has_value()) {
    exec_duration_parameter =
        mojom::NullableUint32::New(exec_duration.value().InSeconds());
  }
  cros_healthd_diagnostics_service_->RunCpuCacheRoutine(
      std::move(exec_duration_parameter),
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunCpuStressRoutine(
    const std::optional<base::TimeDelta>& exec_duration) {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  mojom::NullableUint32Ptr exec_duration_parameter;
  if (exec_duration.has_value()) {
    exec_duration_parameter =
        mojom::NullableUint32::New(exec_duration.value().InSeconds());
  }
  cros_healthd_diagnostics_service_->RunCpuStressRoutine(
      std::move(exec_duration_parameter),
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunDiskReadRoutine(mojom::DiskReadRoutineTypeEnum type,
                                           base::TimeDelta exec_duration,
                                           uint32_t file_size_mb) {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunDiskReadRoutine(
      type, exec_duration.InSeconds(), file_size_mb,
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunDnsLatencyRoutine() {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunDnsLatencyRoutine(
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunDnsResolutionRoutine() {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunDnsResolutionRoutine(
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunDnsResolverPresentRoutine() {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunDnsResolverPresentRoutine(
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunFloatingPointAccuracyRoutine(
    const std::optional<base::TimeDelta>& exec_duration) {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  mojom::NullableUint32Ptr exec_duration_parameter;
  if (exec_duration.has_value()) {
    exec_duration_parameter =
        mojom::NullableUint32::New(exec_duration.value().InSeconds());
  }
  cros_healthd_diagnostics_service_->RunFloatingPointAccuracyRoutine(
      std::move(exec_duration_parameter),
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunGatewayCanBePingedRoutine() {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunGatewayCanBePingedRoutine(
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunHasSecureWiFiConnectionRoutine() {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunHasSecureWiFiConnectionRoutine(
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunHttpFirewallRoutine() {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunHttpFirewallRoutine(
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunHttpsFirewallRoutine() {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunHttpsFirewallRoutine(
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunHttpsLatencyRoutine() {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunHttpsLatencyRoutine(
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunLanConnectivityRoutine() {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunLanConnectivityRoutine(
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunMemoryRoutine(
    std::optional<uint32_t> max_testing_mem_kib) {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunMemoryRoutine(
      max_testing_mem_kib,
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunNvmeSelfTestRoutine(
    mojom::NvmeSelfTestTypeEnum nvme_self_test_type) {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunNvmeSelfTestRoutine(
      nvme_self_test_type,
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunNvmeWearLevelRoutine(
    const std::optional<uint32_t>& wear_level_threshold) {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  mojom::NullableUint32Ptr wear_level_threshold_parameter;
  if (wear_level_threshold.has_value()) {
    wear_level_threshold_parameter =
        mojom::NullableUint32::New(wear_level_threshold.value());
  }
  cros_healthd_diagnostics_service_->RunNvmeWearLevelRoutine(
      std::move(wear_level_threshold_parameter),
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunPrimeSearchRoutine(
    const std::optional<base::TimeDelta>& exec_duration) {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  mojom::NullableUint32Ptr exec_duration_parameter;
  if (exec_duration.has_value()) {
    exec_duration_parameter =
        mojom::NullableUint32::New(exec_duration.value().InSeconds());
  }
  cros_healthd_diagnostics_service_->RunPrimeSearchRoutine(
      std::move(exec_duration_parameter),
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunSignalStrengthRoutine() {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunSignalStrengthRoutine(
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunSmartctlCheckRoutine(
    const std::optional<uint32_t>& percentage_used_threshold) {
  ash::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  ash::cros_healthd::mojom::NullableUint32Ptr
      percentage_used_threshold_parameter;
  if (percentage_used_threshold.has_value()) {
    percentage_used_threshold_parameter =
        ash::cros_healthd::mojom::NullableUint32::New(
            percentage_used_threshold.value());
  }
  cros_healthd_diagnostics_service_->RunSmartctlCheckRoutine(
      std::move(percentage_used_threshold_parameter),
      base::BindOnce(&OnMojoResponseReceived<
                         ash::cros_healthd::mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunUrandomRoutine(
    const std::optional<base::TimeDelta>& length_seconds) {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  mojom::NullableUint32Ptr length_seconds_parameter;
  if (length_seconds.has_value()) {
    length_seconds_parameter =
        mojom::NullableUint32::New(length_seconds.value().InSeconds());
  }
  cros_healthd_diagnostics_service_->RunUrandomRoutine(
      std::move(length_seconds_parameter),
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunVideoConferencingRoutine(
    const std::optional<std::string>& stun_server_hostname) {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunVideoConferencingRoutine(
      stun_server_hostname,
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunArcHttpRoutine() {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunArcHttpRoutine(
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunArcPingRoutine() {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunArcPingRoutine(
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunArcDnsResolutionRoutine() {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunArcDnsResolutionRoutine(
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunSensitiveSensorRoutine() {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunSensitiveSensorRoutine(
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunFingerprintRoutine() {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunFingerprintRoutine(
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunFingerprintAliveRoutine() {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunFingerprintAliveRoutine(
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));

  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunPrivacyScreenRoutine(bool target_state) {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;

  cros_healthd_diagnostics_service_->RunPrivacyScreenRoutine(
      target_state,
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunLedRoutine(mojom::LedName name,
                                      mojom::LedColor color) {
  mojo::PendingReceiver<mojom::LedLitUpRoutineReplier> replier_receiver;
  mojo::PendingRemote<mojom::LedLitUpRoutineReplier> replier_remote(
      replier_receiver.InitWithNewPipeAndPassRemote());
  led_lit_up_routine_replier_ =
      std::make_unique<LedLitUpRoutineReplier>(std::move(replier_receiver));
  led_lit_up_routine_replier_->SetGetColorMatchedHandler(
      base::BindRepeating(&HandleGetLedColorMatchedInvocation));
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunLedLitUpRoutine(
      name, color, std::move(replier_remote),
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunEmmcLifetimeRoutine() {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunEmmcLifetimeRoutine(
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunAudioSetVolumeRoutine(uint64_t node_id,
                                                 uint8_t volume,
                                                 bool mute_on) {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;

  cros_healthd_diagnostics_service_->RunAudioSetVolumeRoutine(
      node_id, volume, mute_on,
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunAudioSetGainRoutine(uint64_t node_id, uint8_t gain) {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;

  cros_healthd_diagnostics_service_->RunAudioSetGainRoutine(
      node_id, gain, /*deprecated_mute_on=*/false,
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();

  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunBluetoothPowerRoutine() {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunBluetoothPowerRoutine(
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunBluetoothDiscoveryRoutine() {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunBluetoothDiscoveryRoutine(
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunBluetoothScanningRoutine(
    const std::optional<base::TimeDelta>& exec_duration) {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  mojom::NullableUint32Ptr exec_duration_parameter;
  if (exec_duration.has_value()) {
    exec_duration_parameter =
        mojom::NullableUint32::New(exec_duration.value().InSeconds());
  }
  cros_healthd_diagnostics_service_->RunBluetoothScanningRoutine(
      std::move(exec_duration_parameter),
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunBluetoothPairingRoutine(
    const std::string& peripheral_id) {
  mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunBluetoothPairingRoutine(
      peripheral_id,
      base::BindOnce(&OnMojoResponseReceived<mojom::RunRoutineResponsePtr>,
                     &response, run_loop.QuitClosure()));
  run_loop.Run();
  return ProcessRoutineResponse(response);
}

void DiagActions::ForceCancelAtPercent(uint32_t percent) {
  CHECK_LE(percent, 100) << "Percent must be <= 100.";
  force_cancel_ = true;
  cancellation_percent_ = percent;
}

bool DiagActions::ProcessRoutineResponse(
    const mojom::RunRoutineResponsePtr& response) {
  if (!response) {
    std::cout << "Unable to run routine. Routine response empty" << std::endl;
    return false;
  }

  id_ = response->id;
  if (id_ == mojom::kFailedToStartId) {
    PrintStatus(response->status);
    auto status_msg = "";
    switch (response->status) {
      case mojom::DiagnosticRoutineStatusEnum::kUnsupported:
        status_msg = "The routine is not supported by the device";
        break;
      case mojom::DiagnosticRoutineStatusEnum::kNotRun:
        status_msg = "The routine is not applicable to the device at this time";
        break;
      default:
        status_msg = "Failed to start routine";
    }
    PrintStatusMessage(status_msg);
    return true;
  }

  return PollRoutineAndProcessResult();
}

bool DiagActions::PollRoutineAndProcessResult() {
  mojom::RoutineUpdatePtr response;
  const base::TimeTicks start_time = tick_clock_->NowTicks();

  do {
    // Poll the routine until it's either interactive and requires user input,
    // or it's noninteractive but no longer running.
    response =
        GetRoutineUpdate(id_, mojom::DiagnosticRoutineCommandEnum::kGetStatus,
                         true /* include_output */);
    std::cout << '\r' << "Progress: " << response->progress_percent
              << std::flush;

    if (force_cancel_ && !response.is_null() &&
        response->progress_percent >= cancellation_percent_) {
      response =
          GetRoutineUpdate(id_, mojom::DiagnosticRoutineCommandEnum::kCancel,
                           true /* include_output */);
      force_cancel_ = false;
    }

    base::RunLoop run_loop;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, run_loop.QuitClosure(), kPollingInterval);
    run_loop.Run();
  } while (
      !response.is_null() &&
      response->routine_update_union->is_noninteractive_update() &&
      response->routine_update_union->get_noninteractive_update()->status ==
          mojom::DiagnosticRoutineStatusEnum::kRunning &&
      tick_clock_->NowTicks() < start_time + kMaximumExecutionTime);

  if (response.is_null()) {
    std::cout << '\n' << "No GetRoutineUpdateResponse received." << std::endl;
    return false;
  }

  if (response->routine_update_union->is_interactive_update()) {
    return ProcessInteractiveResultAndContinue(
        std::move(response->routine_update_union->get_interactive_update()));
  }

  // Noninteractive routines without a status of kRunning must have terminated
  // in some form. Print the update to the console to let the user know.
  std::cout << '\r' << "Progress: " << response->progress_percent << std::endl;
  if (response->output.is_valid()) {
    auto shm_mapping =
        diagnostics::GetReadOnlySharedMemoryMappingFromMojoHandle(
            std::move(response->output));
    if (!shm_mapping.IsValid()) {
      LOG(ERROR) << "Failed to read output.";
      return false;
    }

    auto output = base::JSONReader::Read(std::string(
        shm_mapping.GetMemoryAs<const char>(), shm_mapping.mapped_size()));
    if (!output.has_value()) {
      LOG(ERROR) << "Failed to parse output.";
      return false;
    }

    std::string json;
    base::JSONWriter::WriteWithOptions(
        output.value(), base::JSONWriter::Options::OPTIONS_PRETTY_PRINT, &json);
    std::cout << "Output: " << json << std::endl;
  }

  return ProcessNonInteractiveResultAndEnd(
      std::move(response->routine_update_union->get_noninteractive_update()));
}

bool DiagActions::ProcessInteractiveResultAndContinue(
    mojom::InteractiveRoutineUpdatePtr interactive_result) {
  // Print a newline so we don't overwrite the progress percent.
  std::cout << '\n';
  // Interactive updates require us to print out instructions to the user on the
  // console. Once the user responds by pressing the ENTER key, we need to send
  // a continue command to the routine and restart waiting for results.
  //
  // kCheckLedColor is an exception, which use a pending_remote to communicate
  // with the routine. It should be migrated to the new routine API in the
  // future.
  bool skip_sending_continue_command = false;
  switch (interactive_result->user_message) {
    case mojom::DiagnosticRoutineUserMessageEnum::kUnplugACPower:
      std::cout << "Unplug the AC adapter." << std::endl;
      WaitUntilEnterPressed();
      break;
    case mojom::DiagnosticRoutineUserMessageEnum::kPlugInACPower:
      std::cout << "Plug in the AC adapter." << std::endl;
      WaitUntilEnterPressed();
      break;
    case mojom::DiagnosticRoutineUserMessageEnum::kCheckLedColor:
      // Don't send the continue command because it communicates with the
      // routine through |HandleGetLedColorMatchedInvocation|.
      skip_sending_continue_command = true;
      break;
    case mojom::DiagnosticRoutineUserMessageEnum::kUnknown:
      LOG(ERROR) << "Unknown routine user message enum";
      RemoveRoutine();
      return false;
  }

  if (!skip_sending_continue_command) {
    auto response =
        GetRoutineUpdate(id_, mojom::DiagnosticRoutineCommandEnum::kContinue,
                         false /* include_output */);
  }
  return PollRoutineAndProcessResult();
}

bool DiagActions::ProcessNonInteractiveResultAndEnd(
    mojom::NonInteractiveRoutineUpdatePtr noninteractive_result) {
  mojom::DiagnosticRoutineStatusEnum status = noninteractive_result->status;

  // Clean up the routine if necessary - if the routine never started, then we
  // don't need to remove it.
  if (status != mojom::DiagnosticRoutineStatusEnum::kFailedToStart)
    RemoveRoutine();

  if (!PrintStatus(status))
    return false;

  PrintStatusMessage(noninteractive_result->status_message);

  return true;
}

void DiagActions::RemoveRoutine() {
  auto response =
      GetRoutineUpdate(id_, mojom::DiagnosticRoutineCommandEnum::kRemove,
                       false /* include_output */);

  // Reset |id_|, because it's no longer valid after the routine has been
  // removed.
  id_ = mojom::kFailedToStartId;

  if (response.is_null() ||
      !response->routine_update_union->is_noninteractive_update() ||
      response->routine_update_union->get_noninteractive_update()->status !=
          mojom::DiagnosticRoutineStatusEnum::kRemoved) {
    LOG(ERROR) << "Failed to remove routine: " << id_;
  }
}

bool DiagActions::PrintStatus(mojom::DiagnosticRoutineStatusEnum status) {
  bool status_found = false;
  for (const auto& item : kDiagnosticRoutineReadableStatuses) {
    if (item.status == status) {
      status_found = true;
      std::cout << "Status: " << item.readable_status << std::endl;
      break;
    }
  }

  if (!status_found) {
    LOG(ERROR) << "No human-readable string for status: "
               << static_cast<int>(status);
    return false;
  }

  return true;
}

}  // namespace diagnostics
