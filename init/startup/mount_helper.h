// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_STARTUP_MOUNT_HELPER_H_
#define INIT_STARTUP_MOUNT_HELPER_H_

#include <memory>
#include <stack>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/values.h>
#include <libstorage/platform/platform.h>
#include <libstorage/storage_container/storage_container_factory.h>

#include "init/startup/flags.h"
#include "init/startup/startup_dep_impl.h"
#include "init/tpm_encryption/encryption_key.h"

namespace startup {

class MountVarAndHomeChronosInterface;

// MountHelper contains the functionality for maintaining the mount stack
// and the mounting and umounting of /var and /home/chronos.
// This is the base class for the MountHelper classes. The pure virtual
// functions are defined within the StandardMountHelper, FactoryMountHelper,
// and StandardMountHelper classes.
class MountHelper {
 public:
  // For testing, we can change how we mount encstateful.
  MountHelper(libstorage::Platform* platform,
              StartupDep* startup_dep,
              const Flags* flags,
              const base::FilePath& root,
              std::unique_ptr<MountVarAndHomeChronosInterface> impl,
              libstorage::StorageContainerFactory* storage_container_factory);

  virtual ~MountHelper();

  // Add mount to mount stack.
  void RememberMount(const base::FilePath& mount);
  // On failure unmount all saved mount points and repair stateful.
  void CleanupMountsStack(std::vector<base::FilePath>* mnts);
  // Unmounts the incomplete mount setup during the failure path.
  void CleanupMounts(const std::string& msg);
  // Tries to bind mount, clobbers the stateful partition on failure.
  void BindMountOrFail(const base::FilePath& source,
                       const base::FilePath& target);
  // Mount or unmount home chronos.
  // DoMountVarAndHomeChronos is defined in children of this class.
  // The key is only needed for encrypted stateful.
  bool MountVarAndHomeChronos(std::optional<encryption::EncryptionKey> key);
  bool DoUmountVarAndHomeChronos();

  // Return the storage container factory, used to create filestsytem
  // stack.
  libstorage::StorageContainerFactory* GetStorageContainerFactory() {
    return storage_container_factory_;
  }

  // Bind mount the /var and /home/chronos mounts. The implementation
  // is different for test images and when in factory mode. It also
  // changes depending on the encrypted stateful USE flag.
  virtual bool DoMountVarAndHomeChronos(
      std::optional<encryption::EncryptionKey> key) = 0;

  // Returns a location to save the initial random TPM system key seed.
  // Used only in test mode.
  virtual base::FilePath GetKeyBackupFile() const { return base::FilePath(); }

 protected:
  raw_ptr<libstorage::Platform> platform_;
  raw_ptr<StartupDep> startup_dep_;
  const Flags* flags_;
  const base::FilePath root_;

 private:
  std::stack<base::FilePath> mount_stack_;
  std::unique_ptr<MountVarAndHomeChronosInterface> impl_;
  libstorage::StorageContainerFactory* storage_container_factory_;
};

}  // namespace startup

#endif  // INIT_STARTUP_MOUNT_HELPER_H_
