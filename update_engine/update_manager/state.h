// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_STATE_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_STATE_H_

#include "update_engine/update_manager/config_provider.h"
#include "update_engine/update_manager/device_policy_provider.h"
#include "update_engine/update_manager/random_provider.h"
#include "update_engine/update_manager/shill_provider.h"
#include "update_engine/update_manager/system_provider.h"
#include "update_engine/update_manager/time_provider.h"
#include "update_engine/update_manager/updater_provider.h"

namespace chromeos_update_manager {

// The State class is an interface to the ensemble of providers. This class
// gives visibility of the state providers to policy implementations.
class State {
 public:
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  virtual ~State() {}

  // These methods return the given provider.
  virtual ConfigProvider* config_provider() = 0;
  virtual DevicePolicyProvider* device_policy_provider() = 0;
  virtual RandomProvider* random_provider() = 0;
  virtual ShillProvider* shill_provider() = 0;
  virtual SystemProvider* system_provider() = 0;
  virtual TimeProvider* time_provider() = 0;
  virtual UpdaterProvider* updater_provider() = 0;

 protected:
  State() {}
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_STATE_H_
