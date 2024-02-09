// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_FAKE_CONFIG_PROVIDER_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_FAKE_CONFIG_PROVIDER_H_

#include "update_engine/update_manager/config_provider.h"
#include "update_engine/update_manager/fake_variable.h"

namespace chromeos_update_manager {

// Fake implementation of the ConfigProvider base class.
class FakeConfigProvider : public ConfigProvider {
 public:
  FakeConfigProvider() {}
  FakeConfigProvider(const FakeConfigProvider&) = delete;
  FakeConfigProvider& operator=(const FakeConfigProvider&) = delete;

  FakeVariable<bool>* var_is_oobe_enabled() override {
    return &var_is_oobe_enabled_;
  }

  FakeVariable<bool>* var_is_running_from_minios() override {
    return &var_is_running_from_minios_;
  }

 private:
  FakeVariable<bool> var_is_oobe_enabled_{"is_oobe_enabled",
                                          kVariableModeConst};
  FakeVariable<bool> var_is_running_from_minios_{"is_running_from_minios",
                                                 kVariableModeConst};
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_FAKE_CONFIG_PROVIDER_H_
