// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_FAKE_SYSTEM_PROVIDER_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_FAKE_SYSTEM_PROVIDER_H_

#include "update_engine/update_manager/system_provider.h"

#include <string>

#include "update_engine/update_manager/fake_variable.h"

namespace chromeos_update_manager {

// Fake implementation of the SystemProvider base class.
class FakeSystemProvider : public SystemProvider {
 public:
  FakeSystemProvider() {}
  FakeSystemProvider(const FakeSystemProvider&) = delete;
  FakeSystemProvider& operator=(const FakeSystemProvider&) = delete;

  FakeVariable<bool>* var_is_normal_boot_mode() override {
    return &var_is_normal_boot_mode_;
  }

  FakeVariable<bool>* var_is_official_build() override {
    return &var_is_official_build_;
  }

  FakeVariable<bool>* var_is_oobe_complete() override {
    return &var_is_oobe_complete_;
  }

  FakeVariable<unsigned int>* var_num_slots() override {
    return &var_num_slots_;
  }

  FakeVariable<std::string>* var_kiosk_required_platform_version() override {
    return &var_kiosk_required_platform_version_;
  }

  FakeVariable<base::Version>* var_chromeos_version() override {
    return &var_version_;
  }

  FakeVariable<bool>* var_is_updating() override { return &var_is_updating_; }

 private:
  FakeVariable<bool> var_is_normal_boot_mode_{"is_normal_boot_mode",
                                              kVariableModeConst};
  FakeVariable<bool> var_is_official_build_{"is_official_build",
                                            kVariableModeConst};
  FakeVariable<bool> var_is_oobe_complete_{"is_oobe_complete",
                                           kVariableModePoll};
  FakeVariable<unsigned int> var_num_slots_{"num_slots", kVariableModePoll};
  FakeVariable<std::string> var_kiosk_required_platform_version_{
      "kiosk_required_platform_version", kVariableModePoll};
  FakeVariable<base::Version> var_version_{"chromeos_version",
                                           kVariableModePoll};
  FakeVariable<bool> var_is_updating_{"is_updating", kVariableModeConst};
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_FAKE_SYSTEM_PROVIDER_H_
