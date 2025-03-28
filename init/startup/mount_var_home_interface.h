// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_STARTUP_MOUNT_VAR_HOME_INTERFACE_H_
#define INIT_STARTUP_MOUNT_VAR_HOME_INTERFACE_H_

#include "init/tpm_encryption/encryption_key.h"

namespace startup {
// MountVarAndHomeChronosInterface contains the functionality for mount/umount
// encstateful, encrypted or not.
class MountVarAndHomeChronosInterface {
 public:
  virtual ~MountVarAndHomeChronosInterface() = default;

  // Unmount bind mounts for /var and /home/chronos.
  virtual bool Umount() = 0;
  // Mount bind mounts for /var and /home/chronos.
  virtual bool Mount(std::optional<encryption::EncryptionKey> key) = 0;
};

}  // namespace startup

#endif  // INIT_STARTUP_MOUNT_VAR_HOME_INTERFACE_H_
