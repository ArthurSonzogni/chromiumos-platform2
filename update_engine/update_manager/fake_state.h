// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_FAKE_STATE_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_FAKE_STATE_H_

#include "update_engine/update_manager/fake_config_provider.h"
#include "update_engine/update_manager/fake_device_policy_provider.h"
#include "update_engine/update_manager/fake_random_provider.h"
#include "update_engine/update_manager/fake_shill_provider.h"
#include "update_engine/update_manager/fake_system_provider.h"
#include "update_engine/update_manager/fake_time_provider.h"
#include "update_engine/update_manager/fake_updater_provider.h"
#include "update_engine/update_manager/state.h"

namespace chromeos_update_manager {

// A fake State class that creates fake providers for all the providers.
// This fake can be used in unit testing of Policy subclasses. To fake out the
// value a variable is exposing, just call FakeVariable<T>::SetValue() on the
// variable you fake out. For example:
//
//   FakeState fake_state_;
//   fake_state_.random_provider_->var_seed()->SetValue(new uint64_t(12345));
//
// You can call SetValue more than once and the FakeVariable will take care of
// the memory, but only the last value will remain.
class FakeState : public State {
 public:
  // Creates and initializes the FakeState using fake providers.
  FakeState() {}
  FakeState(const FakeState&) = delete;
  FakeState& operator=(const FakeState&) = delete;

  ~FakeState() override {}

  // Downcasted getters to access the fake instances during testing.
  FakeConfigProvider* config_provider() override { return &config_provider_; }

  FakeDevicePolicyProvider* device_policy_provider() override {
    return &device_policy_provider_;
  }

  FakeRandomProvider* random_provider() override { return &random_provider_; }

  FakeShillProvider* shill_provider() override { return &shill_provider_; }

  FakeSystemProvider* system_provider() override { return &system_provider_; }

  FakeTimeProvider* time_provider() override { return &time_provider_; }

  FakeUpdaterProvider* updater_provider() override {
    return &updater_provider_;
  }

 private:
  FakeConfigProvider config_provider_;
  FakeDevicePolicyProvider device_policy_provider_;
  FakeRandomProvider random_provider_;
  FakeShillProvider shill_provider_;
  FakeSystemProvider system_provider_;
  FakeTimeProvider time_provider_;
  FakeUpdaterProvider updater_provider_;
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_FAKE_STATE_H_
