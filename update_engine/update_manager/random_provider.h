// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_RANDOM_PROVIDER_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_RANDOM_PROVIDER_H_

#include "update_engine/update_manager/provider.h"
#include "update_engine/update_manager/variable.h"

namespace chromeos_update_manager {

// Provider of random values.
class RandomProvider : public Provider {
 public:
  RandomProvider(const RandomProvider&) = delete;
  RandomProvider& operator=(const RandomProvider&) = delete;
  ~RandomProvider() override {}

  // Return a random number every time it is requested. Note that values
  // returned by the variables are cached by the EvaluationContext, so the
  // returned value will be the same during the same policy request. If more
  // random values are needed use a PRNG seeded with this value.
  virtual Variable<uint64_t>* var_seed() = 0;

 protected:
  RandomProvider() {}
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_RANDOM_PROVIDER_H_
