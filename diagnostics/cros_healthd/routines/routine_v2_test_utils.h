// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_ROUTINE_V2_TEST_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_ROUTINE_V2_TEST_UTILS_H_

#include <mojo/public/cpp/bindings/receiver.h>

#include "diagnostics/cros_healthd/routines/base_routine_control.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

// All of the utilities in this file are for use in testing only.

// Returns a callback that will CHECK when invoked. This callback is designed to
// be used in |BaseRoutineControl::SetOnExceptionCallback|.
//
// It uses CHECK rather than a fatal failure of GoogleTest to make the test fail
// as soon as possible in case the callback is invoked within |RunLoop::Run|.
BaseRoutineControl::ExceptionCallback UnexpectedRoutineExceptionCallback();

// Fake Routine observer for testing.
class FakeRoutineObserver : public ash::cros_healthd::mojom::RoutineObserver {
 public:
  FakeRoutineObserver();
  FakeRoutineObserver(const FakeRoutineObserver&) = delete;
  FakeRoutineObserver& operator=(const FakeRoutineObserver&) = delete;
  ~FakeRoutineObserver();

  mojo::Receiver<ash::cros_healthd::mojom::RoutineObserver>& receiver() {
    return receiver_;
  }

  ash::cros_healthd::mojom::RoutineStatePtr& last_routine_state() {
    return last_routine_state_;
  }

 private:
  // mojom::RoutineObserver overrides
  void OnRoutineStateChange(
      ash::cros_healthd::mojom::RoutineStatePtr state) override;

  mojo::Receiver<ash::cros_healthd::mojom::RoutineObserver> receiver_{this};

  ash::cros_healthd::mojom::RoutineStatePtr last_routine_state_ = nullptr;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_ROUTINE_V2_TEST_UTILS_H_
