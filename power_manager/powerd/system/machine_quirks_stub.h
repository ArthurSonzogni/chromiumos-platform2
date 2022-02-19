// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_MACHINE_QUIRKS_STUB_H_
#define POWER_MANAGER_POWERD_SYSTEM_MACHINE_QUIRKS_STUB_H_

#include <base/macros.h>

#include "power_manager/powerd/system/machine_quirks.h"

namespace power_manager {
namespace system {

// Stub implementation of MachineQuirksInterface for tests.
class MachineQuirksStub : public MachineQuirksInterface {
 public:
  MachineQuirksStub();
  MachineQuirksStub(const MachineQuirksStub&) = delete;
  MachineQuirksStub& operator=(const MachineQuirksStub&) = delete;

  ~MachineQuirksStub() override = default;

  void Init(PrefsInterface* prefs) override;
  // MachineQuirksInterface implementation:
  void ApplyQuirksToPrefs() override;
  bool IsSuspendToIdle() override;
  bool IsSuspendBlocked() override;

  void ResetQuirks();
  // Set the bool value to be returned for the idle check.
  void SetSuspendToIdleQuirkDetected(bool value);
  // Set the bool value to be returned for the blocked check.
  void SetSuspendBlockedQuirkDetected(bool value);

 private:
  bool force_idle_ = false;
  bool block_suspend_ = false;
  PrefsInterface* prefs_;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_MACHINE_QUIRKS_STUB_H_
