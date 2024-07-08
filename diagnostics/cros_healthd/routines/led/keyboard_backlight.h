// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_LED_KEYBOARD_BACKLIGHT_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_LED_KEYBOARD_BACKLIGHT_H_

#include <cstdint>
#include <memory>

#include <base/functional/callback_helpers.h>
#include <base/memory/weak_ptr.h>
#include <base/types/expected.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "diagnostics/cros_healthd/routines/interactive_routine_control.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace brillo {
class Error;
}  // namespace brillo

namespace diagnostics {

class Context;

class KeyboardBacklightRoutine final : public InteractiveRoutineControl {
 public:
  static base::expected<std::unique_ptr<BaseRoutineControl>,
                        ash::cros_healthd::mojom::SupportStatusPtr>
  Create(Context* context,
         ash::cros_healthd::mojom::KeyboardBacklightRoutineArgumentPtr arg);
  KeyboardBacklightRoutine(const KeyboardBacklightRoutine&) = delete;
  KeyboardBacklightRoutine& operator=(const KeyboardBacklightRoutine&) = delete;
  ~KeyboardBacklightRoutine() override;

  // Variables to control the brightness percent during routine.
  // Start from `kMinBrightnessPercentToTest`,
  // incremented by `kBrightnessPercentToTestIncrement`,
  // end at `kMaxBrightnessPercentToTest` (inclusive).
  inline static constexpr uint32_t kMinBrightnessPercentToTest = 0u;
  inline static constexpr uint32_t kMaxBrightnessPercentToTest = 100u;
  inline static constexpr uint32_t kBrightnessPercentToTestIncrement = 25u;

  // Seconds to stay in each percent during the routine.
  inline static constexpr base::TimeDelta kTimeToStayAtEachPercent =
      base::Seconds(5);

  // BaseRoutineControl overrides:
  void OnStart() override;

  // InteractiveRoutineControl overrides:
  void OnReplyInquiry(
      ash::cros_healthd::mojom::RoutineInquiryReplyPtr reply) override;

  // Handle the response of getting the brightness percent on start.
  void HandleGetBrightnessOnStart(brillo::Error* err, double percent);

  // Handle the response of setting the brightness percent during testing.
  void HandleSetBrightnessDuringTesting(uint32_t brightness_to_test,
                                        brillo::Error* err);

  // Handle the response of restoring the brightness percent.
  void HandleRestoreBrightness(brillo::Error* err);

  // Handle the response of enabling the ambient light sensor (ALS).
  void HandleEnableAmbientLightSensor(brillo::Error* err);

 protected:
  explicit KeyboardBacklightRoutine(Context* context);

 private:
  enum class TestStep {
    kInitialize = 0,
    kTestBrightness = 1,
    kWaitingForUserConfirmation = 2,
    kRestoreConfig = 3,
    kComplete = 4,  // Should be the last one. New step should be added before
                    // it.
  };

  void RunNextStep();
  // Run keyboard backlight tests by setting different brightness percent and
  // stay for `kTimeToStayPerPercent` seconds.
  void TestBrightness(uint32_t brightness_to_test);
  void RestoreConfig();

  // Context object used to communicate with powerd.
  Context* const context_;
  // The current step of the routine.
  TestStep step_ = TestStep::kInitialize;
  // Whether the routine has passed, confirmed by the user.
  bool routine_passed_ = false;
  // A callback that should be run regardless of the execution status. This
  // callback will enable the ambient light sensor.
  base::ScopedClosureRunner enable_als_closure_;
  // The brightness percent before routine starts.
  double brightness_percent_on_start_ = 0.0;
  // Must be the last class member.
  base::WeakPtrFactory<KeyboardBacklightRoutine> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_LED_KEYBOARD_BACKLIGHT_H_
