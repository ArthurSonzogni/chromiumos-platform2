// Copyright 2013 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_CLOCK_H_
#define UPDATE_ENGINE_COMMON_CLOCK_H_

#include "update_engine/common/clock_interface.h"

namespace chromeos_update_engine {

// Implements a clock.
class Clock : public ClockInterface {
 public:
  Clock() = default;
  Clock(const Clock&) = delete;
  Clock& operator=(const Clock&) = delete;

  base::Time GetWallclockTime() const override;
  base::Time GetMonotonicTime() const override;
  base::Time GetBootTime() const override;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_CLOCK_H_
