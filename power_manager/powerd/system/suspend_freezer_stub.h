// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_SUSPEND_FREEZER_STUB_H_
#define POWER_MANAGER_POWERD_SYSTEM_SUSPEND_FREEZER_STUB_H_

#include "power_manager/powerd/system/suspend_freezer.h"

#include <base/macros.h>

namespace power_manager {
namespace system {

// Stub implementation of SuspendFreezerInterface for use by tests.
class SuspendFreezerStub : public SuspendFreezerInterface {
 public:
  SuspendFreezerStub() = default;
  SuspendFreezerStub(const SuspendFreezerStub&) = delete;
  SuspendFreezerStub& operator=(const SuspendFreezerStub&) = delete;
  ~SuspendFreezerStub() override = default;

  FreezeResult FreezeUserspace(uint64_t wakeup_count,
                               bool wakeup_count_valid) override {
    return FreezeResult::SUCCESS;
  }
  bool ThawUserspace() override { return true; }
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_SUSPEND_FREEZER_STUB_H_
