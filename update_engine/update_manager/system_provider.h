// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_SYSTEM_PROVIDER_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_SYSTEM_PROVIDER_H_

#include <string>

#include <base/version.h>

#include "update_engine/update_manager/provider.h"
#include "update_engine/update_manager/variable.h"

namespace chromeos_update_manager {

// Provider for system information, mostly constant, such as the information
// reported by crossystem, the kernel boot command line and the partition table.
class SystemProvider : public Provider {
 public:
  SystemProvider(const SystemProvider&) = delete;
  SystemProvider& operator=(const SystemProvider&) = delete;
  ~SystemProvider() override {}

  // Returns true if the boot mode is normal or if it's unable to
  // determine the boot mode. Returns false if the boot mode is
  // developer.
  virtual Variable<bool>* var_is_normal_boot_mode() = 0;

  // Returns whether this is an official Chrome OS build.
  virtual Variable<bool>* var_is_official_build() = 0;

  // Returns a variable that tells whether OOBE was completed.
  virtual Variable<bool>* var_is_oobe_complete() = 0;

  // Returns a variable that tells the number of slots in the system.
  virtual Variable<unsigned int>* var_num_slots() = 0;

  // Returns the required platform version of the configured auto launch
  // with zero delay kiosk app if any.
  virtual Variable<std::string>* var_kiosk_required_platform_version() = 0;

  // Chrome OS version number as provided by |ImagePropeties|.
  virtual Variable<base::Version>* var_chromeos_version() = 0;

  // Returns a variable that tells if performing update, otherwise an
  // indicates an installation.
  virtual Variable<bool>* var_is_updating() = 0;

 protected:
  SystemProvider() {}
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_SYSTEM_PROVIDER_H_
