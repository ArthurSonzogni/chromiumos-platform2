// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_STARTUP_MOUNT_VAR_HOME_ENCRYPTED_IMPL_H_
#define INIT_STARTUP_MOUNT_VAR_HOME_ENCRYPTED_IMPL_H_

#include <memory>
#include <stack>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/values.h>
#include <libstorage/platform/platform.h>
#include <libstorage/storage_container/storage_container_factory.h>

#include "init/startup/mount_var_home_interface.h"
#include "init/startup/startup_dep_impl.h"

namespace startup {

// MountVarAndHomeChronosEncryptedImpl implements the encrypted support.
class MountVarAndHomeChronosEncryptedImpl
    : public MountVarAndHomeChronosInterface {
 public:
  MountVarAndHomeChronosEncryptedImpl(
      libstorage::Platform* platform,
      StartupDep* startup_dep,
      libstorage::StorageContainerFactory* container_factory,
      const base::FilePath& root,
      const base::FilePath& stateful);

  // Unmount bind mounts for /var and /home/chronos when encrypted.
  bool Umount() override;
  bool Mount(std::optional<encryption::EncryptionKey> key) override;

 private:
  raw_ptr<libstorage::Platform> platform_;
  raw_ptr<StartupDep> startup_dep_;
  libstorage::StorageContainerFactory* container_factory_;
  const base::FilePath root_;
  const base::FilePath stateful_;
};

}  // namespace startup

#endif  // INIT_STARTUP_MOUNT_VAR_HOME_ENCRYPTED_IMPL_H_
