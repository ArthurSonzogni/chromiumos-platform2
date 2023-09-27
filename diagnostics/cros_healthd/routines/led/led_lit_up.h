// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_LED_LED_LIT_UP_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_LED_LED_LIT_UP_H_

#include <optional>
#include <string>

#include <base/memory/weak_ptr.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "diagnostics/cros_healthd/routines/base_routine_control.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

class LedLitUpV2Routine final : public BaseRoutineControl {
 public:
  LedLitUpV2Routine(Context* context,
                    ash::cros_healthd::mojom::LedLitUpRoutineArgumentPtr arg);
  LedLitUpV2Routine(const LedLitUpV2Routine&) = delete;
  LedLitUpV2Routine& operator=(const LedLitUpV2Routine&) = delete;
  ~LedLitUpV2Routine() override;

  // BaseRoutineControl overrides:
  void OnStart() override;

 private:
  void RunNextStep();
  void ReplierDisconnectHandler();
  void SetLedColorCallback(const std::optional<std::string>& err);
  void GetColorMatchedCallback(bool matched);
  void ResetLedColorCallback(const std::optional<std::string>& err);

  // Context object used to communicate with the executor.
  Context* context_;

  // The target LED.
  ash::cros_healthd::mojom::LedName name_;
  // The target color.
  ash::cros_healthd::mojom::LedColor color_;
  // A replier that can answer whether the actual LED color matches the
  // expected color.
  mojo::Remote<ash::cros_healthd::mojom::LedLitUpRoutineReplier> replier_;

  enum TestStep {
    kInitialize = 0,
    kSetColor = 1,
    kGetColorMatched = 2,
    kResetColor = 3,
    kComplete = 4,  // Should be the last one. New step should be added before
                    // it.
  };
  TestStep step_;

  // The response of |GetColorMatched| from |replier_|.
  bool color_matched_response_ = false;
  // Whether to reset the color in the cleanup.
  bool need_reset_color_in_cleanup_ = false;
  // Must be the last class member.
  base::WeakPtrFactory<LedLitUpV2Routine> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_LED_LED_LIT_UP_H_
