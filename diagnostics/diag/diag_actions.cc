// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/diag/diag_actions.h"

#include <cstdint>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <utility>

#include <base/logging.h>
#include <base/no_destructor.h>
#include <base/run_loop.h>
#include <base/threading/thread_task_runner_handle.h>
#include <base/time/time.h>

#include "diagnostics/common/mojo_utils.h"
#include "diagnostics/cros_healthd_mojo_adapter/cros_healthd_mojo_adapter.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

namespace {

const struct {
  const char* switch_name;
  mojo_ipc::DiagnosticRoutineEnum routine;
} kDiagnosticRoutineSwitches[] = {
    {"battery_capacity", mojo_ipc::DiagnosticRoutineEnum::kBatteryCapacity},
    {"battery_health", mojo_ipc::DiagnosticRoutineEnum::kBatteryHealth},
    {"urandom", mojo_ipc::DiagnosticRoutineEnum::kUrandom},
    {"smartctl_check", mojo_ipc::DiagnosticRoutineEnum::kSmartctlCheck},
    {"ac_power", mojo_ipc::DiagnosticRoutineEnum::kAcPower},
    {"cpu_cache", mojo_ipc::DiagnosticRoutineEnum::kCpuCache},
    {"cpu_stress", mojo_ipc::DiagnosticRoutineEnum::kCpuStress},
    {"floating_point_accuracy",
     mojo_ipc::DiagnosticRoutineEnum::kFloatingPointAccuracy},
    {"nvme_wear_level", mojo_ipc::DiagnosticRoutineEnum::kNvmeWearLevel},
    {"nvme_self_test", mojo_ipc::DiagnosticRoutineEnum::kNvmeSelfTest},
    {"disk_read", mojo_ipc::DiagnosticRoutineEnum::kDiskRead},
    {"prime_search", mojo_ipc::DiagnosticRoutineEnum::kPrimeSearch},
    {"battery_discharge", mojo_ipc::DiagnosticRoutineEnum::kBatteryDischarge}};

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
    {"Unsupported", mojo_ipc::DiagnosticRoutineStatusEnum::kUnsupported}};

const struct {
  const char* readable_user_message;
  mojo_ipc::DiagnosticRoutineUserMessageEnum user_message_enum;
} kDiagnosticRoutineReadableUserMessages[] = {
    {"Unplug the AC adapter.",
     mojo_ipc::DiagnosticRoutineUserMessageEnum::kUnplugACPower},
    {"Plug in the AC adapter.",
     mojo_ipc::DiagnosticRoutineUserMessageEnum::kPlugInACPower}};

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

}  // namespace

DiagActions::DiagActions(base::TimeDelta polling_interval,
                         base::TimeDelta maximum_execution_time,
                         const base::TickClock* tick_clock)
    : kPollingInterval(polling_interval),
      kMaximumExecutionTime(maximum_execution_time) {
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
  auto reply = adapter_.GetAvailableRoutines();
  for (auto routine : reply) {
    std::cout << "Available routine: " << GetSwitchFromRoutine(routine)
              << std::endl;
  }

  return true;
}

bool DiagActions::ActionRunAcPowerRoutine(
    mojo_ipc::AcPowerStatusEnum expected_status,
    const base::Optional<std::string>& expected_power_type) {
  auto response =
      adapter_.RunAcPowerRoutine(expected_status, expected_power_type);
  CHECK(response) << "No RunRoutineResponse received.";
  id_ = response->id;
  return PollRoutineAndProcessResult();
}

bool DiagActions::ActionRunBatteryCapacityRoutine(uint32_t low_mah,
                                                  uint32_t high_mah) {
  auto response = adapter_.RunBatteryCapacityRoutine(low_mah, high_mah);
  CHECK(response) << "No RunRoutineResponse received.";
  id_ = response->id;
  return PollRoutineAndProcessResult();
}

bool DiagActions::ActionRunBatteryDischargeRoutine(
    base::TimeDelta exec_duration, uint32_t maximum_discharge_percent_allowed) {
  auto response = adapter_.RunBatteryDischargeRoutine(
      exec_duration, maximum_discharge_percent_allowed);
  CHECK(response) << "No RunRoutineResponse received.";
  id_ = response->id;
  return PollRoutineAndProcessResult();
}

bool DiagActions::ActionRunBatteryHealthRoutine(
    uint32_t maximum_cycle_count, uint32_t percent_battery_wear_allowed) {
  auto response = adapter_.RunBatteryHealthRoutine(
      maximum_cycle_count, percent_battery_wear_allowed);
  CHECK(response) << "No RunRoutineResponse received.";
  id_ = response->id;
  return PollRoutineAndProcessResult();
}

bool DiagActions::ActionRunCpuCacheRoutine(base::TimeDelta exec_duration) {
  auto response = adapter_.RunCpuCacheRoutine(exec_duration);
  CHECK(response) << "No RunRoutineResponse received.";
  id_ = response->id;
  return PollRoutineAndProcessResult();
}

bool DiagActions::ActionRunCpuStressRoutine(base::TimeDelta exec_duration) {
  auto response = adapter_.RunCpuStressRoutine(exec_duration);
  CHECK(response) << "No RunRoutineResponse received.";
  id_ = response->id;
  return PollRoutineAndProcessResult();
}

bool DiagActions::ActionRunDiskReadRoutine(
    mojo_ipc::DiskReadRoutineTypeEnum type,
    base::TimeDelta exec_duration,
    uint32_t file_size_mb) {
  auto response =
      adapter_.RunDiskReadRoutine(type, exec_duration, file_size_mb);
  id_ = response->id;
  CHECK(response) << "No RunRoutineResponse received.";
  return PollRoutineAndProcessResult();
}

bool DiagActions::ActionRunFloatingPointAccuracyRoutine(
    base::TimeDelta exec_duration) {
  auto response = adapter_.RunFloatingPointAccuracyRoutine(exec_duration);
  CHECK(response) << "No RunRoutineResponse received.";
  id_ = response->id;
  return PollRoutineAndProcessResult();
}

bool DiagActions::ActionRunNvmeSelfTestRoutine(
    mojo_ipc::NvmeSelfTestTypeEnum nvme_self_test_type) {
  auto response = adapter_.RunNvmeSelfTestRoutine(nvme_self_test_type);
  CHECK(response) << "No RunRoutineResponse received.";
  id_ = response->id;
  return PollRoutineAndProcessResult();
}

bool DiagActions::ActionRunNvmeWearLevelRoutine(uint32_t wear_level_threshold) {
  auto response = adapter_.RunNvmeWearLevelRoutine(wear_level_threshold);
  CHECK(response) << "No RunRoutineResponse received.";
  id_ = response->id;
  return PollRoutineAndProcessResult();
}

bool DiagActions::ActionRunPrimeSearchRoutine(base::TimeDelta exec_duration,
                                              uint64_t max_num) {
  auto response = adapter_.RunPrimeSearchRoutine(exec_duration, max_num);
  id_ = response->id;
  CHECK(response) << "No RunRoutineResponse received.";
  return PollRoutineAndProcessResult();
}

bool DiagActions::ActionRunSmartctlCheckRoutine() {
  auto response = adapter_.RunSmartctlCheckRoutine();
  CHECK(response) << "No RunRoutineResponse received.";
  id_ = response->id;
  return PollRoutineAndProcessResult();
}

bool DiagActions::ActionRunUrandomRoutine(uint32_t length_seconds) {
  auto response = adapter_.RunUrandomRoutine(length_seconds);
  CHECK(response) << "No RunRoutineResponse received.";
  id_ = response->id;
  return PollRoutineAndProcessResult();
}

void DiagActions::ForceCancelAtPercent(uint32_t percent) {
  CHECK_LE(percent, 100) << "Percent must be <= 100.";
  force_cancel_ = true;
  cancellation_percent_ = percent;
}

bool DiagActions::PollRoutineAndProcessResult() {
  mojo_ipc::RoutineUpdatePtr response;
  const base::TimeTicks start_time = tick_clock_->NowTicks();

  do {
    // Poll the routine until it's either interactive and requires user input,
    // or it's noninteractive but no longer running.
    response = adapter_.GetRoutineUpdate(
        id_, mojo_ipc::DiagnosticRoutineCommandEnum::kGetStatus,
        true /* include_output */);
    std::cout << "Progress: " << response->progress_percent << std::endl;

    if (force_cancel_ && !response.is_null() &&
        response->progress_percent >= cancellation_percent_) {
      response = adapter_.GetRoutineUpdate(
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
    std::cout << "No GetRoutineUpdateResponse received." << std::endl;
    return false;
  }

  if (response->routine_update_union->is_interactive_update()) {
    return ProcessInteractiveResultAndContinue(
        std::move(response->routine_update_union->get_interactive_update()));
  }

  // Noninteractive routines without a status of kRunning must have terminated
  // in some form. Print the update to the console to let the user know.
  std::cout << "Progress: " << response->progress_percent << std::endl;
  if (response->output.is_valid()) {
    auto shared_memory = diagnostics::GetReadOnlySharedMemoryFromMojoHandle(
        std::move(response->output));
    if (shared_memory) {
      std::cout << "Output: "
                << std::string(
                       static_cast<const char*>(shared_memory->memory()),
                       shared_memory->mapped_size())
                << std::endl;
    } else {
      LOG(ERROR) << "Failed to read output.";
      return false;
    }
  }

  return ProcessNonInteractiveResultAndEnd(
      std::move(response->routine_update_union->get_noninteractive_update()));
}

bool DiagActions::ProcessInteractiveResultAndContinue(
    mojo_ipc::InteractiveRoutineUpdatePtr interactive_result) {
  // Interactive updates require us to print out instructions to the user on the
  // console. Once the user responds by pressing the ENTER key, we need to send
  // a continue command to the routine and restart waiting for results.
  bool user_message_found = false;
  mojo_ipc::DiagnosticRoutineUserMessageEnum user_message =
      interactive_result->user_message;
  for (const auto& item : kDiagnosticRoutineReadableUserMessages) {
    if (item.user_message_enum == user_message) {
      user_message_found = true;
      std::cout << item.readable_user_message << std::endl
                << "Press ENTER to continue." << std::endl;
      break;
    }
  }

  if (!user_message_found) {
    LOG(ERROR) << "No human-readable string for user message: "
               << static_cast<int>(user_message);
    RemoveRoutine();
    return false;
  }

  std::string dummy;
  std::getline(std::cin, dummy);

  auto response = adapter_.GetRoutineUpdate(
      id_, mojo_ipc::DiagnosticRoutineCommandEnum::kContinue,
      false /* include_output */);
  return PollRoutineAndProcessResult();
}

bool DiagActions::ProcessNonInteractiveResultAndEnd(
    mojo_ipc::NonInteractiveRoutineUpdatePtr noninteractive_result) {
  bool status_found = false;
  mojo_ipc::DiagnosticRoutineStatusEnum status = noninteractive_result->status;

  // Clean up the routine if necessary - if the routine never started, then we
  // don't need to remove it.
  if (status != mojo_ipc::DiagnosticRoutineStatusEnum::kFailedToStart)
    RemoveRoutine();

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

  std::cout << "Status message: " << noninteractive_result->status_message
            << std::endl;

  return true;
}

void DiagActions::RemoveRoutine() {
  auto response = adapter_.GetRoutineUpdate(
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

}  // namespace diagnostics
