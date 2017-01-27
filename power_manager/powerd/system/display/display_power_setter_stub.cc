// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/display/display_power_setter_stub.h"

namespace power_manager {
namespace system {

DisplayPowerSetterStub::DisplayPowerSetterStub()
    : clock_(nullptr),
      state_(chromeos::DISPLAY_POWER_ALL_ON),
      num_power_calls_(0),
      dimmed_(false) {}

DisplayPowerSetterStub::~DisplayPowerSetterStub() {}

void DisplayPowerSetterStub::SetDisplayPower(chromeos::DisplayPowerState state,
                                             base::TimeDelta delay) {
  state_ = state;
  delay_ = delay;
  num_power_calls_++;
  last_set_display_power_time_ =
      clock_ ? clock_->GetCurrentTime() : base::TimeTicks::Now();
}

void DisplayPowerSetterStub::SetDisplaySoftwareDimming(bool dimmed) {
  dimmed_ = dimmed;
}

}  // namespace system
}  // namespace power_manager
