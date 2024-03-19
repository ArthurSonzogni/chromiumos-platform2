// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_INTERACTIVE_ROUTINE_CONTROL_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_INTERACTIVE_ROUTINE_CONTROL_H_

#include <string>

#include "diagnostics/cros_healthd/routines/base_routine_control.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom-forward.h"

namespace diagnostics {

// Implements the RoutineControl interface for routines that involve
// interactions.
class InteractiveRoutineControl : public BaseRoutineControl {
 public:
  InteractiveRoutineControl();
  InteractiveRoutineControl(const InteractiveRoutineControl&) = delete;
  InteractiveRoutineControl& operator=(const InteractiveRoutineControl&) =
      delete;
  ~InteractiveRoutineControl() override;

  // ash::cros_healthd::mojom::RoutineControl overrides
  void ReplyInquiry(
      ash::cros_healthd::mojom::RoutineInquiryReplyPtr reply) final;

 protected:
  // Set the state to waiting for an inquiry, this can only be called if the
  // state is currently running.
  void SetWaitingInquiryState(
      const std::string& message,
      ash::cros_healthd::mojom::RoutineInquiryPtr inquiry);

 private:
  // The derived classes override this to perform the actions to resume the
  // routine that requests an inquiry in the waiting state.
  virtual void OnReplyInquiry(
      ash::cros_healthd::mojom::RoutineInquiryReplyPtr reply) = 0;

  // Declared as private to prevent usages in derived classes.
  ash::cros_healthd::mojom::RoutineStatePtr& mutable_state();
  void NotifyObserver();
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_INTERACTIVE_ROUTINE_CONTROL_H_
