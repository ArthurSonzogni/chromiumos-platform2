// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/memory_and_cpu/urandom.h"

#include <cstdint>
#include <memory>
#include <utility>

#include <base/task/single_thread_task_runner.h>
#include <base/time/time.h>

#include "diagnostics/cros_healthd/routines/memory_and_cpu/constants.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/utils/resource_queue.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

}  // namespace

UrandomRoutine::UrandomRoutine(Context* context,
                               const mojom::UrandomRoutineArgumentPtr& arg)
    : context_(context) {
  exec_duration_ = arg->exec_duration.value_or(kDefaultCpuRoutineRuntime);

  if (exec_duration_.InSeconds() < 1) {
    DLOG(INFO)
        << "Routine run time must be larger than 1 second. Running minimum "
           "exec duration of 1 second instead.";
    exec_duration_ = base::Seconds(1);
  }
  CHECK(context_);
}

UrandomRoutine::~UrandomRoutine() = default;

void UrandomRoutine::OnStart() {
  SetWaitingState(mojom::RoutineStateWaiting::Reason::kWaitingToBeScheduled,
                  "Waiting for memory and CPU resource");
  context_->memory_cpu_resource_queue()->Enqueue(
      base::BindOnce(&UrandomRoutine::Run, weak_ptr_factory_.GetWeakPtr()));
}

void UrandomRoutine::Run(
    base::ScopedClosureRunner notify_resource_queue_finished) {
  SetRunningState();

  context_->executor()->RunUrandom(
      exec_duration_, scoped_process_control_.BindNewPipeAndPassReceiver(),
      base::BindOnce(&UrandomRoutine::OnFinished,
                     weak_ptr_factory_.GetWeakPtr()));
  scoped_process_control_.AddOnTerminateCallback(
      std::move(notify_resource_queue_finished));

  start_ticks_ = base::TimeTicks::Now();
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&UrandomRoutine::UpdatePercentage,
                     weak_ptr_factory_.GetWeakPtr()),
      exec_duration_ / 100);
}

void UrandomRoutine::OnFinished(bool passed) {
  scoped_process_control_.Reset();
  SetFinishedState(passed, /*detail=*/nullptr);
}

void UrandomRoutine::UpdatePercentage() {
  uint32_t percentage = static_cast<uint32_t>(
      100.0 * (base::TimeTicks::Now() - start_ticks_) / exec_duration_);
  if (percentage > state()->percentage && percentage < 100) {
    SetPercentage(percentage);
  }

  if (state()->percentage < 99) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&UrandomRoutine::UpdatePercentage,
                       weak_ptr_factory_.GetWeakPtr()),
        exec_duration_ / 100);
  }
}

}  // namespace diagnostics
