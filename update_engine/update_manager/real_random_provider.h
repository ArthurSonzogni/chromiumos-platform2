// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_REAL_RANDOM_PROVIDER_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_REAL_RANDOM_PROVIDER_H_

#include <memory>

#include "update_engine/update_manager/random_provider.h"

namespace chromeos_update_manager {

// RandomProvider implementation class.
class RealRandomProvider : public RandomProvider {
 public:
  RealRandomProvider() {}
  RealRandomProvider(const RealRandomProvider&) = delete;
  RealRandomProvider& operator=(const RealRandomProvider&) = delete;

  Variable<uint64_t>* var_seed() override { return var_seed_.get(); }

  // Initializes the provider and returns whether it succeeded.
  bool Init();

 private:
  // The seed() scoped variable.
  std::unique_ptr<Variable<uint64_t>> var_seed_;
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_REAL_RANDOM_PROVIDER_H_
