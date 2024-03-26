// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_LED_LED_LIT_UP_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_LED_LED_LIT_UP_H_

#include <memory>
#include <optional>
#include <string>

#include <base/memory/weak_ptr.h>
#include <base/types/expected.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "diagnostics/cros_healthd/routines/interactive_routine_control.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

class Context;

class LedLitUpRoutine final : public InteractiveRoutineControl {
 public:
  static base::expected<std::unique_ptr<BaseRoutineControl>,
                        ash::cros_healthd::mojom::SupportStatusPtr>
  Create(Context* context,
         ash::cros_healthd::mojom::LedLitUpRoutineArgumentPtr arg);
  LedLitUpRoutine(const LedLitUpRoutine&) = delete;
  LedLitUpRoutine& operator=(const LedLitUpRoutine&) = delete;
  ~LedLitUpRoutine() override;

  // BaseRoutineControl overrides:
  void OnStart() override;

  // InteractiveRoutineControl overrides:
  void OnReplyInquiry(
      ash::cros_healthd::mojom::RoutineInquiryReplyPtr reply) override;

 protected:
  LedLitUpRoutine(Context* context,
                  ash::cros_healthd::mojom::LedLitUpRoutineArgumentPtr arg);

 private:
  enum class TestStep {
    kInitialize = 0,
    kSetColor = 1,
    kWaitingForLedState = 2,
    kResetColor = 3,
    kComplete = 4,  // Should be the last one. New step should be added before
                    // it.
  };

  void RunNextStep();
  void SetLedColorCallback(const std::optional<std::string>& err);
  void ResetLedColorCallback(const std::optional<std::string>& err);

  // Context object used to communicate with the executor.
  Context* const context_;
  // The target LED.
  const ash::cros_healthd::mojom::LedName name_;
  // The target color.
  const ash::cros_healthd::mojom::LedColor color_;
  // The current step of the routine.
  TestStep step_ = TestStep::kInitialize;
  // Whether the LED lights up in the correct color, replied from the client.
  bool led_color_correct_ = false;
  // Whether to reset the color in the cleanup.
  bool need_reset_color_in_cleanup_ = false;
  // Must be the last class member.
  base::WeakPtrFactory<LedLitUpRoutine> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_LED_LED_LIT_UP_H_
