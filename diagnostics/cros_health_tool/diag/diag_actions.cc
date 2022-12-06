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
#include <base/threading/thread_task_runner_handle.h>
#include <base/time/time.h>

#include "diagnostics/common/mojo_utils.h"
#include "diagnostics/cros_health_tool/diag/diag_constants.h"
#include "diagnostics/cros_healthd_mojo_adapter/cros_healthd_mojo_adapter.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::ash::cros_healthd::mojom;

const struct {
  const char* readable_status;
  mojo_ipc::DiagnosticRoutineStatusEnum status;
} kDiagnosticRoutineReadableStatuses[] = {
    {"Ready", mojo_ipc::DiagnosticRoutineStatusEnum::kReady},
    {"Running", mojo_ipc::DiagnosticRoutineStatusEnum::kRunning},
    {"Waiting", mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting},
    {"Passed", mojo_ipc::DiagnosticRoutineStatusEnum::kPassed},
    {"Failed", mojo_ipc::DiagnosticRoutineStatusEnum::kFailed},
    {"Error", mojo_ipc::DiagnosticRoutineStatusEnum::kError},
    {"Cancelled", mojo_ipc::DiagnosticRoutineStatusEnum::kCancelled},
    {"Failed to start", mojo_ipc::DiagnosticRoutineStatusEnum::kFailedToStart},
    {"Removed", mojo_ipc::DiagnosticRoutineStatusEnum::kRemoved},
    {"Cancelling", mojo_ipc::DiagnosticRoutineStatusEnum::kCancelling},
    {"Unsupported", mojo_ipc::DiagnosticRoutineStatusEnum::kUnsupported},
    {"Not run", mojo_ipc::DiagnosticRoutineStatusEnum::kNotRun}};

std::string GetSwitchFromRoutine(mojo_ipc::DiagnosticRoutineEnum routine) {
  static base::NoDestructor<
      std::map<mojo_ipc::DiagnosticRoutineEnum, std::string>>
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
    mojo_ipc::LedLitUpRoutineReplier::GetColorMatchedCallback callback) {
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

}  // namespace

DiagActions::DiagActions(base::TimeDelta polling_interval,
                         base::TimeDelta maximum_execution_time,
                         const base::TickClock* tick_clock)
    : adapter_(CrosHealthdMojoAdapter::Create()),
      kPollingInterval(polling_interval),
      kMaximumExecutionTime(maximum_execution_time) {
  DCHECK(adapter_);

  if (tick_clock) {
    tick_clock_ = tick_clock;
  } else {
    default_tick_clock_ = std::make_unique<base::DefaultTickClock>();
    tick_clock_ = default_tick_clock_.get();
  }
  DCHECK(tick_clock_);
}

DiagActions::~DiagActions() = default;

bool DiagActions::ActionGetRoutines() {
  auto reply = adapter_->GetAvailableRoutines();
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
    mojo_ipc::AcPowerStatusEnum expected_status,
    const std::optional<std::string>& expected_power_type) {
  auto response =
      adapter_->RunAcPowerRoutine(expected_status, expected_power_type);
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunBatteryCapacityRoutine() {
  auto response = adapter_->RunBatteryCapacityRoutine();
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunBatteryChargeRoutine(
    base::TimeDelta exec_duration, uint32_t minimum_charge_percent_required) {
  auto response = adapter_->RunBatteryChargeRoutine(
      exec_duration, minimum_charge_percent_required);
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunBatteryDischargeRoutine(
    base::TimeDelta exec_duration, uint32_t maximum_discharge_percent_allowed) {
  auto response = adapter_->RunBatteryDischargeRoutine(
      exec_duration, maximum_discharge_percent_allowed);
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunBatteryHealthRoutine() {
  auto response = adapter_->RunBatteryHealthRoutine();
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunCaptivePortalRoutine() {
  auto response = adapter_->RunCaptivePortalRoutine();
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunCpuCacheRoutine(
    const std::optional<base::TimeDelta>& exec_duration) {
  auto response = adapter_->RunCpuCacheRoutine(exec_duration);
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunCpuStressRoutine(
    const std::optional<base::TimeDelta>& exec_duration) {
  auto response = adapter_->RunCpuStressRoutine(exec_duration);
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunDiskReadRoutine(
    mojo_ipc::DiskReadRoutineTypeEnum type,
    base::TimeDelta exec_duration,
    uint32_t file_size_mb) {
  auto response =
      adapter_->RunDiskReadRoutine(type, exec_duration, file_size_mb);
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunDnsLatencyRoutine() {
  auto response = adapter_->RunDnsLatencyRoutine();
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunDnsResolutionRoutine() {
  auto response = adapter_->RunDnsResolutionRoutine();
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunDnsResolverPresentRoutine() {
  auto response = adapter_->RunDnsResolverPresentRoutine();
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunFloatingPointAccuracyRoutine(
    const std::optional<base::TimeDelta>& exec_duration) {
  auto response = adapter_->RunFloatingPointAccuracyRoutine(exec_duration);
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunGatewayCanBePingedRoutine() {
  auto response = adapter_->RunGatewayCanBePingedRoutine();
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunHasSecureWiFiConnectionRoutine() {
  auto response = adapter_->RunHasSecureWiFiConnectionRoutine();
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunHttpFirewallRoutine() {
  auto response = adapter_->RunHttpFirewallRoutine();
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunHttpsFirewallRoutine() {
  auto response = adapter_->RunHttpsFirewallRoutine();
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunHttpsLatencyRoutine() {
  auto response = adapter_->RunHttpsLatencyRoutine();
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunLanConnectivityRoutine() {
  auto response = adapter_->RunLanConnectivityRoutine();
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunMemoryRoutine() {
  auto response = adapter_->RunMemoryRoutine();
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunNvmeSelfTestRoutine(
    mojo_ipc::NvmeSelfTestTypeEnum nvme_self_test_type) {
  auto response = adapter_->RunNvmeSelfTestRoutine(nvme_self_test_type);
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunNvmeWearLevelRoutine(
    const std::optional<uint32_t>& wear_level_threshold) {
  auto response = adapter_->RunNvmeWearLevelRoutine(wear_level_threshold);
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunPrimeSearchRoutine(
    const std::optional<base::TimeDelta>& exec_duration) {
  auto response = adapter_->RunPrimeSearchRoutine(exec_duration);
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunSignalStrengthRoutine() {
  auto response = adapter_->RunSignalStrengthRoutine();
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunSmartctlCheckRoutine(
    const std::optional<uint32_t>& percentage_used_threshold) {
  auto response = adapter_->RunSmartctlCheckRoutine(percentage_used_threshold);
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunUrandomRoutine(
    const std::optional<base::TimeDelta>& length_seconds) {
  auto response = adapter_->RunUrandomRoutine(length_seconds);
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunVideoConferencingRoutine(
    const std::optional<std::string>& stun_server_hostname) {
  auto response = adapter_->RunVideoConferencingRoutine(stun_server_hostname);
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunArcHttpRoutine() {
  auto response = adapter_->RunArcHttpRoutine();
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunArcPingRoutine() {
  auto response = adapter_->RunArcPingRoutine();
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunArcDnsResolutionRoutine() {
  auto response = adapter_->RunArcDnsResolutionRoutine();
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunSensitiveSensorRoutine() {
  auto response = adapter_->RunSensitiveSensorRoutine();
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunFingerprintRoutine() {
  auto response = adapter_->RunFingerprintRoutine();
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunFingerprintAliveRoutine() {
  auto response = adapter_->RunFingerprintAliveRoutine();
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunPrivacyScreenRoutine(bool target_state) {
  auto response = adapter_->RunPrivacyScreenRoutine(target_state);
  return ProcessRoutineResponse(response);
}

bool DiagActions::ActionRunLedRoutine(mojo_ipc::LedName name,
                                      mojo_ipc::LedColor color) {
  mojo::PendingReceiver<mojo_ipc::LedLitUpRoutineReplier> replier_receiver;
  mojo::PendingRemote<mojo_ipc::LedLitUpRoutineReplier> replier_remote(
      replier_receiver.InitWithNewPipeAndPassRemote());
  led_lit_up_routine_replier_ =
      std::make_unique<LedLitUpRoutineReplier>(std::move(replier_receiver));
  led_lit_up_routine_replier_->SetGetColorMatchedHandler(
      base::BindRepeating(&HandleGetLedColorMatchedInvocation));
  auto response =
      adapter_->RunLedLitUpRoutine(name, color, std::move(replier_remote));
  return ProcessRoutineResponse(response);
}

void DiagActions::ForceCancelAtPercent(uint32_t percent) {
  CHECK_LE(percent, 100) << "Percent must be <= 100.";
  force_cancel_ = true;
  cancellation_percent_ = percent;
}

bool DiagActions::ProcessRoutineResponse(
    const mojo_ipc::RunRoutineResponsePtr& response) {
  if (!response) {
    std::cout << "Unable to run routine. Routine response empty" << std::endl;
    return false;
  }

  id_ = response->id;
  if (id_ == mojo_ipc::kFailedToStartId) {
    PrintStatus(response->status);
    auto status_msg = "";
    switch (response->status) {
      case mojo_ipc::DiagnosticRoutineStatusEnum::kUnsupported:
        status_msg = "The routine is not supported by the device";
        break;
      case mojo_ipc::DiagnosticRoutineStatusEnum::kNotRun:
        status_msg = "The routine is not applicable to the device at this time";
        break;
      default:
        status_msg = "Failed to start routine";
    }
    std::cout << "Status Message: " << status_msg << std::endl;
    return true;
  }

  return PollRoutineAndProcessResult();
}

bool DiagActions::PollRoutineAndProcessResult() {
  mojo_ipc::RoutineUpdatePtr response;
  const base::TimeTicks start_time = tick_clock_->NowTicks();

  do {
    // Poll the routine until it's either interactive and requires user input,
    // or it's noninteractive but no longer running.
    response = adapter_->GetRoutineUpdate(
        id_, mojo_ipc::DiagnosticRoutineCommandEnum::kGetStatus,
        true /* include_output */);
    std::cout << '\r' << "Progress: " << response->progress_percent
              << std::flush;

    if (force_cancel_ && !response.is_null() &&
        response->progress_percent >= cancellation_percent_) {
      response = adapter_->GetRoutineUpdate(
          id_, mojo_ipc::DiagnosticRoutineCommandEnum::kCancel,
          true /* include_output */);
      force_cancel_ = false;
    }

    base::RunLoop run_loop;
    base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
        FROM_HERE, run_loop.QuitClosure(), kPollingInterval);
    run_loop.Run();
  } while (
      !response.is_null() &&
      response->routine_update_union->is_noninteractive_update() &&
      response->routine_update_union->get_noninteractive_update()->status ==
          mojo_ipc::DiagnosticRoutineStatusEnum::kRunning &&
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
    mojo_ipc::InteractiveRoutineUpdatePtr interactive_result) {
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
    case mojo_ipc::DiagnosticRoutineUserMessageEnum::kUnplugACPower:
      std::cout << "Unplug the AC adapter." << std::endl;
      WaitUntilEnterPressed();
      break;
    case mojo_ipc::DiagnosticRoutineUserMessageEnum::kPlugInACPower:
      std::cout << "Plug in the AC adapter." << std::endl;
      WaitUntilEnterPressed();
      break;
    case mojo_ipc::DiagnosticRoutineUserMessageEnum::kCheckLedColor:
      // Don't send the continue command because it communicates with the
      // routine through |HandleGetLedColorMatchedInvocation|.
      skip_sending_continue_command = true;
      break;
    case mojo_ipc::DiagnosticRoutineUserMessageEnum::kUnknown:
      LOG(ERROR) << "Unknown routine user message enum";
      RemoveRoutine();
      return false;
  }

  if (!skip_sending_continue_command) {
    auto response = adapter_->GetRoutineUpdate(
        id_, mojo_ipc::DiagnosticRoutineCommandEnum::kContinue,
        false /* include_output */);
  }
  return PollRoutineAndProcessResult();
}

bool DiagActions::ProcessNonInteractiveResultAndEnd(
    mojo_ipc::NonInteractiveRoutineUpdatePtr noninteractive_result) {
  mojo_ipc::DiagnosticRoutineStatusEnum status = noninteractive_result->status;

  // Clean up the routine if necessary - if the routine never started, then we
  // don't need to remove it.
  if (status != mojo_ipc::DiagnosticRoutineStatusEnum::kFailedToStart)
    RemoveRoutine();

  if (!PrintStatus(status))
    return false;

  std::cout << "Status message: " << noninteractive_result->status_message
            << std::endl;

  return true;
}

void DiagActions::RemoveRoutine() {
  auto response = adapter_->GetRoutineUpdate(
      id_, mojo_ipc::DiagnosticRoutineCommandEnum::kRemove,
      false /* include_output */);

  // Reset |id_|, because it's no longer valid after the routine has been
  // removed.
  id_ = mojo_ipc::kFailedToStartId;

  if (response.is_null() ||
      !response->routine_update_union->is_noninteractive_update() ||
      response->routine_update_union->get_noninteractive_update()->status !=
          mojo_ipc::DiagnosticRoutineStatusEnum::kRemoved) {
    LOG(ERROR) << "Failed to remove routine: " << id_;
  }
}

bool DiagActions::PrintStatus(mojo_ipc::DiagnosticRoutineStatusEnum status) {
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
