// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/memory_and_cpu/prime_search.h"

#include <cstdint>
#include <utility>

#include <base/check.h>
#include <base/task/single_thread_task_runner.h>
#include <base/time/time.h>

#include "diagnostics/cros_healthd/routine_parameter_fetcher.h"
#include "diagnostics/cros_healthd/routines/memory_and_cpu/constants.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

const uint64_t kPrimeSearchDefaultMaxNum = 1000000;

}  // namespace

PrimeSearchRoutine::PrimeSearchRoutine(
    Context* context,
    const ash::cros_healthd::mojom::PrimeSearchRoutineArgumentPtr& arg)
    : context_(context) {
  exec_duration_ = arg->exec_duration.value_or(kDefaultCpuStressRuntime);

  if (exec_duration_.InSeconds() < 1) {
    LOG(ERROR)
        << "Routine run time must be larger than 1 second. Running minimum "
           "exec duration of 1 second instead.";
    exec_duration_ = base::Seconds(1);
  }
  CHECK(context_);
  RoutineParameterFetcher parameter_fetcher(context_->cros_config());
  std::optional<uint64_t> max_num;
  parameter_fetcher.GetPrimeSearchParameters(&max_num);

  if (!max_num.has_value()) {
    max_num_ = kPrimeSearchDefaultMaxNum;
  } else {
    if (max_num.value() <= 1) {
      LOG(ERROR)
          << "Cros config value for prime search maximum number should be "
             "larger than 1";
      max_num_ = 2;
    } else {
      max_num_ = max_num.value();
    }
  }

  CHECK(exec_duration_.InSeconds() > 0)
      << "Routine run time must be larger than 0";
  CHECK(max_num_ > 1) << "Routine max num must be larger than 1";
}

PrimeSearchRoutine::~PrimeSearchRoutine() = default;

void PrimeSearchRoutine::OnStart() {
  SetWaitingState(mojom::RoutineStateWaiting::Reason::kWaitingToBeScheduled,
                  "Waiting for memory and CPU resource");
  context_->memory_cpu_resource_queue()->Enqueue(
      base::BindOnce(&PrimeSearchRoutine::Run, weak_ptr_factory_.GetWeakPtr()));
}

void PrimeSearchRoutine::Run(
    base::ScopedClosureRunner notify_resource_queue_finished) {
  SetRunningState();

  context_->executor()->RunPrimeSearch(
      exec_duration_, max_num_,
      scoped_process_control_.BindNewPipeAndPassReceiver(),
      base::BindOnce(&PrimeSearchRoutine::OnFinished,
                     weak_ptr_factory_.GetWeakPtr()));
  scoped_process_control_.AddOnTerminateCallback(
      std::move(notify_resource_queue_finished));

  start_ticks_ = tick_clock_.NowTicks();
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&PrimeSearchRoutine::UpdatePercentage,
                     weak_ptr_factory_.GetWeakPtr()),
      exec_duration_ / 100);
}

void PrimeSearchRoutine::OnFinished(bool passed) {
  scoped_process_control_.Reset();
  SetFinishedState(passed, mojom::RoutineDetail::NewPrimeSearch(
                               mojom::PrimeSearchRoutineDetail::New()));
}

void PrimeSearchRoutine::UpdatePercentage() {
  uint32_t percentage = static_cast<uint32_t>(
      100.0 * (tick_clock_.NowTicks() - start_ticks_) / exec_duration_);
  if (percentage > state()->percentage && percentage < 100) {
    SetPercentage(percentage);
  }

  if (state()->percentage < 99) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&PrimeSearchRoutine::UpdatePercentage,
                       weak_ptr_factory_.GetWeakPtr()),
        exec_duration_ / 100);
  }
}

}  // namespace diagnostics
