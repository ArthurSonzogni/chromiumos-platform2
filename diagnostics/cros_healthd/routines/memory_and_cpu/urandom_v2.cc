// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/memory_and_cpu/urandom_v2.h"

#include <cstdint>
#include <memory>
#include <utility>

#include <base/time/time.h>
#include <base/task/single_thread_task_runner.h>

#include "diagnostics/cros_healthd/routines/memory_and_cpu/constants.h"
#include "diagnostics/cros_healthd/utils/resource_queue.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

}  // namespace

UrandomRoutineV2::UrandomRoutineV2(Context* context,
                                   const mojom::UrandomRoutineArgumentPtr& arg)
    : context_(context) {
  exec_duration_ = arg->exec_duration.value_or(kDefaultCpuRoutineRuntime);

  if (exec_duration_.InSeconds() < 1) {
    LOG(ERROR)
        << "Routine run time must be larger than 1 second. Running minimum "
           "exec duration of 1 second instead.";
    exec_duration_ = base::Seconds(1);
  }
  CHECK(context_);
}

UrandomRoutineV2::~UrandomRoutineV2() = default;

void UrandomRoutineV2::OnStart() {
  SetWaitingState(mojom::RoutineStateWaiting::Reason::kWaitingToBeScheduled,
                  "Waiting for memory and CPU resource");
  context_->memory_cpu_resource_queue()->Enqueue(
      base::BindOnce(&UrandomRoutineV2::Run, weak_ptr_factory_.GetWeakPtr()));
}

void UrandomRoutineV2::Run(
    base::ScopedClosureRunner notify_resource_queue_finished) {
  SetRunningState();

  context_->executor()->RunUrandom(
      exec_duration_, scoped_process_control_.BindNewPipeAndPassReceiver(),
      base::BindOnce(&UrandomRoutineV2::OnFinished,
                     weak_ptr_factory_.GetWeakPtr()));
  scoped_process_control_.AddOnTerminateCallback(
      std::move(notify_resource_queue_finished));

  start_ticks_ = base::TimeTicks::Now();
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&UrandomRoutineV2::UpdatePercentage,
                     weak_ptr_factory_.GetWeakPtr()),
      exec_duration_ / 100);
}

void UrandomRoutineV2::OnFinished(bool passed) {
  scoped_process_control_.Reset();
  SetFinishedState(passed, mojom::RoutineDetail::NewUrandom(
                               mojom::UrandomRoutineDetail::New()));
}

void UrandomRoutineV2::UpdatePercentage() {
  uint32_t percentage = static_cast<uint32_t>(
      100.0 * (base::TimeTicks::Now() - start_ticks_) / exec_duration_);
  if (percentage > state()->percentage && percentage < 100) {
    SetPercentage(percentage);
  }

  if (state()->percentage < 99) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&UrandomRoutineV2::UpdatePercentage,
                       weak_ptr_factory_.GetWeakPtr()),
        exec_duration_ / 100);
  }
}

}  // namespace diagnostics
