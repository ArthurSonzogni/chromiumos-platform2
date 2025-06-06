// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_SUSPEND_CONFIGURATOR_STUB_H_
#define POWER_MANAGER_POWERD_SYSTEM_SUSPEND_CONFIGURATOR_STUB_H_

#include "power_manager/powerd/system/suspend_configurator.h"

namespace power_manager::system {

// Stub implementation of SuspendConfiguratorInterface for use by tests.
class SuspendConfiguratorStub : public SuspendConfiguratorInterface {
 public:
  SuspendConfiguratorStub() = default;
  SuspendConfiguratorStub(const SuspendConfiguratorStub&) = delete;
  SuspendConfiguratorStub& operator=(const SuspendConfiguratorStub&) = delete;

  ~SuspendConfiguratorStub() override = default;

  // SuspendConfiguratorInterface implementation.
  uint64_t PrepareForSuspend(const base::TimeDelta& suspend_duration) override {
    return 0;
  }

  bool UndoPrepareForSuspend(const base::TimeDelta& duration) override {
    return true;
  }
};

}  // namespace power_manager::system

#endif  // POWER_MANAGER_POWERD_SYSTEM_SUSPEND_CONFIGURATOR_STUB_H_
