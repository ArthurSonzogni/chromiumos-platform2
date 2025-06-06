// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_FAKE_RANDOM_PROVIDER_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_FAKE_RANDOM_PROVIDER_H_

#include "update_engine/update_manager/fake_variable.h"
#include "update_engine/update_manager/random_provider.h"

namespace chromeos_update_manager {

// Fake implementation of the RandomProvider base class.
class FakeRandomProvider : public RandomProvider {
 public:
  FakeRandomProvider() {}
  FakeRandomProvider(const FakeRandomProvider&) = delete;
  FakeRandomProvider& operator=(const FakeRandomProvider&) = delete;

  FakeVariable<uint64_t>* var_seed() override { return &var_seed_; }

 private:
  FakeVariable<uint64_t> var_seed_{"seed", kVariableModePoll};
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_FAKE_RANDOM_PROVIDER_H_
