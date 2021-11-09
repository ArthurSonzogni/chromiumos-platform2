// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "power_manager/powerd/system/machine_quirks_stub.h"

#include <base/logging.h>
#include <base/notreached.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/common/prefs.h"

namespace power_manager {
namespace system {

MachineQuirksStub::MachineQuirksStub() {
  ResetQuirks();
}

void MachineQuirksStub::ApplyQuirksToPrefs(PrefsInterface* prefs) {
  DCHECK(prefs);

  if (IsSuspendBlocked()) {
    prefs->SetInt64(kDisableIdleSuspendPref, 1);
  }

  if (IsSuspendToIdle()) {
    prefs->SetInt64(kSuspendToIdlePref, 1);
  }
}

bool MachineQuirksStub::IsSuspendToIdle() {
  return force_idle_;
}

bool MachineQuirksStub::IsSuspendBlocked() {
  return block_suspend_;
}

void MachineQuirksStub::ResetQuirks() {
  force_idle_ = false;
  block_suspend_ = false;
}

void MachineQuirksStub::SetSuspendToIdleQuirkDetected(bool value) {
  force_idle_ = value;
}

void MachineQuirksStub::SetSuspendBlockedQuirkDetected(bool value) {
  block_suspend_ = value;
}

}  // namespace system
}  // namespace power_manager
