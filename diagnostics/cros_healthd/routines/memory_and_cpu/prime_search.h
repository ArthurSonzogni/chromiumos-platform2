// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_MEMORY_AND_CPU_PRIME_SEARCH_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_MEMORY_AND_CPU_PRIME_SEARCH_H_

#include <cstdint>

#include <base/functional/callback_helpers.h>
#include <base/memory/weak_ptr.h>
#include <base/time/default_tick_clock.h>
#include <base/time/time.h>

#include "diagnostics/cros_healthd/executor/utils/scoped_process_control.h"
#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/routines/base_routine_control.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom-forward.h"

namespace diagnostics {

// The prime search routine checks that the device's CPU can calculate
// correctly.
class PrimeSearchRoutine final : public BaseRoutineControl {
 public:
  explicit PrimeSearchRoutine(
      Context* context,
      const ash::cros_healthd::mojom::PrimeSearchRoutineArgumentPtr& arg);
  PrimeSearchRoutine(const PrimeSearchRoutine&) = delete;
  PrimeSearchRoutine& operator=(const PrimeSearchRoutine&) = delete;
  ~PrimeSearchRoutine() override;

  // BaseRoutineControl overrides:
  void OnStart() override;

 private:
  // The |Run| function is added to the memory and cpu resource queue as a
  // callback and will be called when memory and cpu resource is available.
  void Run(base::ScopedClosureRunner notify_resource_queue_finished);

  // Set the finished state once the delegate finish running.
  void OnFinished(bool passed);

  // Update the percentage progress of the routine.
  void UpdatePercentage();

  // Unowned. Should outlive this instance.
  Context* const context_ = nullptr;
  // A scoped version of process control that manages the lifetime of the
  // prime search delegate process.
  ScopedProcessControl scoped_process_control_;
  // The execution duration of the prime search program.
  base::TimeDelta exec_duration_;
  // The execution duration of the prime search program.
  uint64_t max_num_;
  // |start_ticks| records the time when the routine began. This is used with
  // |exec_duration_| to report on progress percentage.
  base::TimeTicks start_ticks_;
  // |tick_clock| is used to get the current time tick for percentage
  // calculation.
  base::DefaultTickClock tick_clock_;

  // Must be the last class member.
  base::WeakPtrFactory<PrimeSearchRoutine> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_MEMORY_AND_CPU_PRIME_SEARCH_H_
