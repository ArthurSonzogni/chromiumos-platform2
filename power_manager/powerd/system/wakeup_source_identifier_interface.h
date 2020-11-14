// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_WAKEUP_SOURCE_IDENTIFIER_INTERFACE_H_
#define POWER_MANAGER_POWERD_SYSTEM_WAKEUP_SOURCE_IDENTIFIER_INTERFACE_H_

#include <map>
#include <memory>
#include <set>
#include <string>

#include <base/files/file_path.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/system/udev_subsystem_observer.h"

namespace power_manager {
namespace system {

// Monitors device sys paths to identify the potential wakeup reason. Monitors
// only if the |wakeup_device_path| points to a directory with power/wakeup
// property.

class WakeupSourceIdentifierInterface {
 public:
  WakeupSourceIdentifierInterface() {}
  WakeupSourceIdentifierInterface(const WakeupSourceIdentifierInterface&) =
      delete;
  WakeupSourceIdentifierInterface& operator=(
      const WakeupSourceIdentifierInterface&) = delete;

  virtual ~WakeupSourceIdentifierInterface() {}

  // Should be called at the beginning of a new suspend request.
  virtual void PrepareForSuspendRequest() = 0;

  // Should be called at the end of a suspend request.
  virtual void HandleResume() = 0;

  // Returns true if any of the input devices' wakeup counts differed (compared
  // to the pre-suspend wakeup counts).
  virtual bool InputDeviceCausedLastWake() const = 0;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_WAKEUP_SOURCE_IDENTIFIER_INTERFACE_H_
