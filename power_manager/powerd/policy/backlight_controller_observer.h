// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_POLICY_BACKLIGHT_CONTROLLER_OBSERVER_H_
#define POWER_MANAGER_POWERD_POLICY_BACKLIGHT_CONTROLLER_OBSERVER_H_

#include "power_manager/powerd/policy/backlight_controller.h"

namespace power_manager {
namespace policy {

// Interface for observing changes made by BacklightControllers.
class BacklightControllerObserver {
 public:
  // Invoked when the brightness level is changed.  |brightness_percent| is the
  // current brightness in the range [0.0, 100.0].
  virtual void OnBrightnessChanged(
      double brightness_percent,
      BacklightController::BrightnessChangeCause cause,
      BacklightController* source) {}

 protected:
  virtual ~BacklightControllerObserver() {}
};

}  // namespace policy
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_POLICY_BACKLIGHT_CONTROLLER_OBSERVER_H_
