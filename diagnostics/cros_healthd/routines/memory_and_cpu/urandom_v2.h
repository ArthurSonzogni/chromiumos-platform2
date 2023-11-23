// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_MEMORY_AND_CPU_URANDOM_V2_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_MEMORY_AND_CPU_URANDOM_V2_H_

#include <cstdint>

#include <base/functional/callback_helpers.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>

#include "diagnostics/cros_healthd/executor/utils/scoped_process_control.h"
#include "diagnostics/cros_healthd/routines/base_routine_control.h"
#include "diagnostics/cros_healthd/system/context.h"

namespace diagnostics {

// The urandom routine checks that the device can repeatedly read from urandom
// correctly.
class UrandomRoutineV2 final : public BaseRoutineControl {
 public:
  explicit UrandomRoutineV2(
      Context* context,
      const ash::cros_healthd::mojom::UrandomRoutineArgumentPtr& arg);
  UrandomRoutineV2(const UrandomRoutineV2&) = delete;
  UrandomRoutineV2& operator=(const UrandomRoutineV2&) = delete;
  ~UrandomRoutineV2() override;

  // BaseRoutineControl overrides:
  void OnStart() override;

 private:
  // The `Run` function is added to the memory and CPU resource queue as a
  // callback and will be called when memory and CPU resource is available.
  void Run(base::ScopedClosureRunner notify_resource_queue_finished);

  // Set the finished state once the delegate finish running.
  void OnFinished(bool passed);

  // Update the percentage progress of the routine.
  void UpdatePercentage();

  // Unowned. Should outlive this instance.
  Context* const context_ = nullptr;
  // A scoped version of process control that manages the lifetime of the
  // urandom delegate process.
  ScopedProcessControl scoped_process_control_;
  // The execution duration of the urandom program.
  base::TimeDelta exec_duration_;
  // `start_ticks_` records the time when the routine began. This is used with
  // `exec_duration_` to report on progress percentage.
  base::TimeTicks start_ticks_;

  // Must be the last class member.
  base::WeakPtrFactory<UrandomRoutineV2> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_MEMORY_AND_CPU_URANDOM_V2_H_
