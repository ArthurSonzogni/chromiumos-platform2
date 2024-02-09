// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_REAL_CONFIG_PROVIDER_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_REAL_CONFIG_PROVIDER_H_

#include <memory>

#include "update_engine/common/hardware_interface.h"
#include "update_engine/update_manager/config_provider.h"
#include "update_engine/update_manager/generic_variables.h"

namespace chromeos_update_manager {

// ConfigProvider concrete implementation.
class RealConfigProvider : public ConfigProvider {
 public:
  explicit RealConfigProvider(
      chromeos_update_engine::HardwareInterface* hardware)
      : hardware_(hardware) {}
  RealConfigProvider(const RealConfigProvider&) = delete;
  RealConfigProvider& operator=(const RealConfigProvider&) = delete;

  // Initializes the provider and returns whether it succeeded.
  bool Init();

  Variable<bool>* var_is_oobe_enabled() override {
    return var_is_oobe_enabled_.get();
  }

  Variable<bool>* var_is_running_from_minios() override {
    return var_is_running_from_minios_.get();
  }

 private:
  std::unique_ptr<ConstCopyVariable<bool>> var_is_oobe_enabled_;
  std::unique_ptr<ConstCopyVariable<bool>> var_is_running_from_minios_;

  chromeos_update_engine::HardwareInterface* hardware_;
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_REAL_CONFIG_PROVIDER_H_
