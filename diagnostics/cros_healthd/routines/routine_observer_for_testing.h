// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_ROUTINE_OBSERVER_FOR_TESTING_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_ROUTINE_OBSERVER_FOR_TESTING_H_

#include <optional>

#include <base/functional/callback_forward.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

class RoutineObserverForTesting
    : public ash::cros_healthd::mojom::RoutineObserver {
 public:
  RoutineObserverForTesting();
  RoutineObserverForTesting(const RoutineObserverForTesting&) = delete;
  RoutineObserverForTesting& operator=(const RoutineObserverForTesting&) =
      delete;
  ~RoutineObserverForTesting() override;

  // ash::cros_healthd::mojom::RoutineObserver overrides:
  void OnRoutineStateChange(
      ash::cros_healthd::mojom::RoutineStatePtr state) override;

  // Wait for the routine to be in the finished state.
  void WaitUntilRoutineFinished();

  // Wait for the routine to be in the waiting state.
  void WaitUntilRoutineWaiting();

  ash::cros_healthd::mojom::RoutineStatePtr state_;
  mojo::Receiver<ash::cros_healthd::mojom::RoutineObserver> receiver_{this};

 private:
  // The config to invoke the given callback when the routine state satisfies
  // certain conditions.
  struct StateTriggeredAction {
    // Whether the state satisfies the required condition.
    base::RepeatingCallback<bool(
        const ash::cros_healthd::mojom::RoutineStatePtr&)>
        is_condition_satisfied;
    // Called when |is_condition_satisfied| returns |true|.
    base::OnceClosure on_condition_satisfied;
  };

  std::optional<StateTriggeredAction> state_action_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_ROUTINE_OBSERVER_FOR_TESTING_H_
