// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_BACKLIGHT_OBSERVER_H_
#define POWER_MANAGER_POWERD_SYSTEM_BACKLIGHT_OBSERVER_H_

#include <base/observer_list_types.h>

namespace power_manager {
namespace system {

class BacklightInterface;

// Interface for observing changes to a BacklightInterface object.
class BacklightObserver : public base::CheckedObserver {
 public:
  // Called when |backlight|'s underlying backlight device is added or removed.
  virtual void OnBacklightDeviceChanged(BacklightInterface* backlight) = 0;

 protected:
  virtual ~BacklightObserver() = default;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_BACKLIGHT_OBSERVER_H_
