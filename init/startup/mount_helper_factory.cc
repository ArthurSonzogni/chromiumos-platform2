// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/startup/mount_helper_factory.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/values.h>
#include <libcrossystem/crossystem.h>
#include <libstorage/platform/platform.h>
#include <libstorage/storage_container/storage_container_factory.h>

#include "init/startup/factory_mode_mount_helper.h"
#include "init/startup/flags.h"
#include "init/startup/mount_helper.h"
#include "init/startup/mount_var_home_encrypted_impl.h"
#include "init/startup/mount_var_home_unencrypted_impl.h"
#include "init/startup/standard_mount_helper.h"
#include "init/startup/startup_dep_impl.h"
#include "init/startup/test_mode_mount_helper.h"

namespace startup {

MountHelperFactory::MountHelperFactory(libstorage::Platform* platform,
                                       StartupDep* startup_dep,
                                       const Flags& flags,
                                       const base::FilePath& root,
                                       const base::FilePath& stateful,
                                       const base::FilePath& lsb_file)
    : platform_(platform),
      startup_dep_(startup_dep),
      flags_(flags),
      root_(root),
      stateful_(stateful),
      lsb_file_(lsb_file) {}

// Generate the mount helper class to use by determining whether a device
// is in dev mode, running a test image, and in factory mode. These different
// possible device configurations need different implementations of the
// functions DoMountVarAndHomeChronos and DoUmountVarAndHomeChronos.
// In the previous bash version of chromeos_startup, these different function
// implementations came from loading dev_utils.sh, test_utils.sh,
// factory_utils.sh and factory_utils.sh.
std::unique_ptr<MountHelper> MountHelperFactory::Generate(
    std::unique_ptr<libstorage::StorageContainerFactory>
        storage_container_factory) {
  bool dev_mode = InDevMode(platform_->GetCrosssystem());
  bool is_test_image = IsTestImage(platform_, lsb_file_);
  bool is_factory_mode = IsFactoryMode(platform_, root_, stateful_);

  // Use factory mount helper.
  if (dev_mode && is_test_image && is_factory_mode) {
    return std::make_unique<FactoryModeMountHelper>(
        platform_, startup_dep_, flags_, root_, stateful_,
        std::make_unique<MountVarAndHomeChronosUnencryptedImpl>(
            platform_, startup_dep_, root_, stateful_),
        std::move(storage_container_factory));
  }

  if (dev_mode && is_test_image) {
    if (USE_ENCRYPTED_STATEFUL && flags_.encstateful) {
      return std::make_unique<TestModeMountHelper>(
          platform_, startup_dep_, flags_, root_, stateful_,
          std::make_unique<MountVarAndHomeChronosEncryptedImpl>(
              platform_, startup_dep_, storage_container_factory.get(), root_,
              stateful_),
          std::move(storage_container_factory));
    } else {
      return std::make_unique<TestModeMountHelper>(
          platform_, startup_dep_, flags_, root_, stateful_,
          std::make_unique<MountVarAndHomeChronosUnencryptedImpl>(
              platform_, startup_dep_, root_, stateful_),
          std::move(storage_container_factory));
    }
  }

  if (USE_ENCRYPTED_STATEFUL && flags_.encstateful) {
    return std::make_unique<StandardMountHelper>(
        platform_, startup_dep_, flags_, root_, stateful_,
        std::make_unique<MountVarAndHomeChronosEncryptedImpl>(
            platform_, startup_dep_, storage_container_factory.get(), root_,
            stateful_),
        std::move(storage_container_factory));
  }
  return std::make_unique<StandardMountHelper>(
      platform_, startup_dep_, flags_, root_, stateful_,
      std::make_unique<MountVarAndHomeChronosUnencryptedImpl>(
          platform_, startup_dep_, root_, stateful_),
      std::move(storage_container_factory));
}

}  // namespace startup
