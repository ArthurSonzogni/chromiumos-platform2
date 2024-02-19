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

#include "diagnostics/cros_healthd/routines/base_routine_control.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

class Context;

class LedLitUpRoutine final : public BaseRoutineControl {
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

 protected:
  LedLitUpRoutine(Context* context,
                  ash::cros_healthd::mojom::LedLitUpRoutineArgumentPtr arg);

 private:
  enum class TestStep {
    kInitialize = 0,
    kSetColor = 1,
    kGetColorMatched = 2,
    kResetColor = 3,
    kComplete = 4,  // Should be the last one. New step should be added before
                    // it.
  };

  void RunNextStep();
  void ReplierDisconnectHandler();
  void SetLedColorCallback(const std::optional<std::string>& err);
  void GetColorMatchedCallback(bool matched);
  void ResetLedColorCallback(const std::optional<std::string>& err);

  // Context object used to communicate with the executor.
  Context* const context_;
  // The target LED.
  const ash::cros_healthd::mojom::LedName name_;
  // The target color.
  const ash::cros_healthd::mojom::LedColor color_;
  // A replier that can answer whether the actual LED color matches the
  // expected color.
  mojo::Remote<ash::cros_healthd::mojom::LedLitUpRoutineReplier> replier_;
  // The current step of the routine.
  TestStep step_ = TestStep::kInitialize;
  // The response of |GetColorMatched| from |replier_|.
  bool color_matched_response_ = false;
  // Whether to reset the color in the cleanup.
  bool need_reset_color_in_cleanup_ = false;
  // Must be the last class member.
  base::WeakPtrFactory<LedLitUpRoutine> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_LED_LED_LIT_UP_H_
