// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_REAL_TIME_PROVIDER_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_REAL_TIME_PROVIDER_H_

#include <memory>

#include <base/time/time.h>

#include "update_engine/update_manager/time_provider.h"

namespace chromeos_update_manager {

// TimeProvider concrete implementation.
class RealTimeProvider : public TimeProvider {
 public:
  RealTimeProvider() = default;
  RealTimeProvider(const RealTimeProvider&) = delete;
  RealTimeProvider& operator=(const RealTimeProvider&) = delete;

  // Initializes the provider and returns whether it succeeded.
  bool Init();

  Variable<base::Time>* var_curr_date() override {
    return var_curr_date_.get();
  }

  Variable<int>* var_curr_hour() override { return var_curr_hour_.get(); }

  Variable<int>* var_curr_minute() override { return var_curr_minute_.get(); }

 private:
  std::unique_ptr<Variable<base::Time>> var_curr_date_;
  std::unique_ptr<Variable<int>> var_curr_hour_;
  std::unique_ptr<Variable<int>> var_curr_minute_;
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_REAL_TIME_PROVIDER_H_
