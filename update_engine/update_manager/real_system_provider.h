// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_REAL_SYSTEM_PROVIDER_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_REAL_SYSTEM_PROVIDER_H_

#include <memory>
#include <string>

#include <base/version.h>

#include "update_engine/update_manager/system_provider.h"

namespace org {
namespace chromium {
class KioskAppServiceInterfaceProxyInterface;
}  // namespace chromium
}  // namespace org

namespace chromeos_update_manager {

// SystemProvider concrete implementation.
class RealSystemProvider : public SystemProvider {
 public:
  RealSystemProvider(
      org::chromium::KioskAppServiceInterfaceProxyInterface* kiosk_app_proxy)
      : kiosk_app_proxy_(kiosk_app_proxy) {}
  RealSystemProvider(const RealSystemProvider&) = delete;
  RealSystemProvider& operator=(const RealSystemProvider&) = delete;

  // Initializes the provider and returns whether it succeeded.
  bool Init();

  Variable<bool>* var_is_normal_boot_mode() override {
    return var_is_normal_boot_mode_.get();
  }

  Variable<bool>* var_is_official_build() override {
    return var_is_official_build_.get();
  }

  Variable<bool>* var_is_oobe_complete() override {
    return var_is_oobe_complete_.get();
  }

  Variable<unsigned int>* var_num_slots() override {
    return var_num_slots_.get();
  }

  Variable<std::string>* var_kiosk_required_platform_version() override {
    return var_kiosk_required_platform_version_.get();
  }

  Variable<base::Version>* var_chromeos_version() override {
    return var_chromeos_version_.get();
  }

  Variable<bool>* var_is_updating() override { return var_is_updating_.get(); }

 private:
  bool GetKioskAppRequiredPlatformVersion(
      std::string* required_platform_version);

  std::unique_ptr<Variable<bool>> var_is_normal_boot_mode_;
  std::unique_ptr<Variable<bool>> var_is_official_build_;
  std::unique_ptr<Variable<bool>> var_is_oobe_complete_;
  std::unique_ptr<Variable<unsigned int>> var_num_slots_;
  std::unique_ptr<Variable<std::string>> var_kiosk_required_platform_version_;
  std::unique_ptr<Variable<base::Version>> var_chromeos_version_;
  std::unique_ptr<Variable<bool>> var_is_updating_;

  org::chromium::KioskAppServiceInterfaceProxyInterface* const kiosk_app_proxy_;
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_REAL_SYSTEM_PROVIDER_H_
