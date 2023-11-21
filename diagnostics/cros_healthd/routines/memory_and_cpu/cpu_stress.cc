// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/memory_and_cpu/cpu_stress.h"

#include <cstdint>
#include <utility>

#include <base/task/single_thread_task_runner.h>

#include "diagnostics/base/file_utils.h"
#include "diagnostics/cros_healthd/routines/memory_and_cpu/constants.h"
#include "diagnostics/cros_healthd/utils/memory_info.h"
#include "diagnostics/cros_healthd/utils/resource_queue.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

}  // namespace

CpuStressRoutine::CpuStressRoutine(
    Context* context, const mojom::CpuStressRoutineArgumentPtr& arg)
    : context_(context) {
  exec_duration_ = arg->exec_duration.value_or(kDefaultCpuStressRuntime);

  if (exec_duration_.InSeconds() < 1) {
    LOG(ERROR) << "Routine run time must be larger than 0. Running default "
                  "exec duration instead.";
    exec_duration_ = kDefaultCpuStressRuntime;
  }
  CHECK(context_);
}

CpuStressRoutine::~CpuStressRoutine() = default;

void CpuStressRoutine::OnStart() {
  SetWaitingState(mojom::RoutineStateWaiting::Reason::kWaitingToBeScheduled,
                  "Waiting for memory and CPU resource");
  context_->memory_cpu_resource_queue()->Enqueue(
      base::BindOnce(&CpuStressRoutine::Run, weak_ptr_factory_.GetWeakPtr()));
}

void CpuStressRoutine::Run(
    base::ScopedClosureRunner notify_resource_queue_finished) {
  auto memory_info = MemoryInfo::ParseFrom(GetRootDir());
  if (!memory_info.has_value()) {
    RaiseException("Memory info not found");
    return;
  }

  uint32_t available_mem_kib = memory_info.value().available_memory_kib;

  // Early check and raise exception if system doesn't have enough memory to
  // run a basic stressapptest test.
  if (available_mem_kib < kCpuMemoryRoutineReservedSizeKiB +
                              kStressAppTestRoutineMinimumRequiredKiB) {
    RaiseException("Not enough memory to run stressapptest.");
    return;
  }

  CHECK(available_mem_kib >= kCpuMemoryRoutineReservedSizeKiB);
  uint32_t testing_mem_kib =
      available_mem_kib - kCpuMemoryRoutineReservedSizeKiB;
  uint32_t testing_mem_mib = testing_mem_kib / 1024;
  SetRunningState();

  context_->executor()->RunStressAppTest(
      testing_mem_mib, exec_duration_.InSeconds(),
      mojom::StressAppTestType::kCpuStress,
      scoped_process_control_.BindNewPipeAndPassReceiver());
  scoped_process_control_.AddOnTerminateCallback(
      std::move(notify_resource_queue_finished));

  scoped_process_control_->GetReturnCode(mojo::WrapCallbackWithDropHandler(
      base::BindOnce(&CpuStressRoutine::HandleGetReturnCode,
                     weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce(&CpuStressRoutine::RaiseException,
                     weak_ptr_factory_.GetWeakPtr(),
                     "process control disconnected before routine finished")));

  start_ticks_ = tick_clock_.NowTicks();
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&CpuStressRoutine::UpdatePercentage,
                     weak_ptr_factory_.GetWeakPtr()),
      exec_duration_ / 100);
}

void CpuStressRoutine::HandleGetReturnCode(int return_code) {
  scoped_process_control_.Reset();
  bool passed = return_code == EXIT_SUCCESS;
  SetFinishedState(passed, mojom::RoutineDetail::NewCpuStress(
                               mojom::CpuStressRoutineDetail::New()));
}

void CpuStressRoutine::UpdatePercentage() {
  uint32_t percentage = static_cast<uint32_t>(
      100.0 * (tick_clock_.NowTicks() - start_ticks_) / exec_duration_);
  if (percentage > state()->percentage && percentage < 100) {
    SetPercentage(percentage);
  }

  if (state()->percentage < 99) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&CpuStressRoutine::UpdatePercentage,
                       weak_ptr_factory_.GetWeakPtr()),
        exec_duration_ / 100);
  }
}

}  // namespace diagnostics
