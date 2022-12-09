// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_MEMORY_MEMORY_V2_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_MEMORY_MEMORY_V2_H_

#include <memory>
#include <string>

#include <base/functional/callback_forward.h>
#include <base/functional/callback_helpers.h>
#include <base/memory/weak_ptr.h>
#include <base/time/default_tick_clock.h>
#include <base/time/tick_clock.h>
#include <base/time/time.h>

#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/routines/base_routine_control.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

// The memory routine checks that the device's memory is working correctly.
class MemoryRoutineV2 final : public BaseRoutineControl {
 public:
  explicit MemoryRoutineV2(Context* context);
  MemoryRoutineV2(const MemoryRoutineV2&) = delete;
  MemoryRoutineV2& operator=(const MemoryRoutineV2&) = delete;
  ~MemoryRoutineV2() override;

  // BaseRoutineControl overrides:
  void OnStart() override;

 private:
  // Unowned. Should outlive this instance.
  Context* const context_ = nullptr;
  // Once the memory resource is finished (when memtester finish running), run
  // this callback to notify the resource queue of resource availability.
  base::ScopedClosureRunner notify_resource_queue_finished_;

  // Must be the last class member.
  base::WeakPtrFactory<MemoryRoutineV2> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_MEMORY_MEMORY_V2_H_
