// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_STARTUP_MOUNT_VAR_HOME_IMPL_H_
#define INIT_STARTUP_MOUNT_VAR_HOME_IMPL_H_

#include <memory>
#include <stack>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/values.h>
#include <libstorage/platform/platform.h>

#include "init/startup/mount_var_home_interface.h"
#include "init/startup/startup_dep_impl.h"

namespace startup {

// MountHelper contains the functionality for maintaining the mount stack
// and the mounting and umounting of /var and /home/chronos.
// This is the base class for the MountHelper classes. The pure virtual
// functions are defined within the StandardMountHelper, FactoryMountHelper,
// and StandardMountHelper classes.
class MountVarAndHomeChronosImpl : public MountVarAndHomeChronosInterface {
 public:
  MountVarAndHomeChronosImpl(libstorage::Platform* platform,
                             StartupDep* startup_dep,
                             const base::FilePath& root,
                             const base::FilePath& stateful);

  // Unmount bind mounts for /var and /home/chronos when encrypted.
  bool UmountEncrypted() override;
  // Unmount bind mounts for /var and /home/chronos when unencrypted.
  bool UmountUnencrypted() override;
  bool MountEncrypted() override;
  bool MountUnencrypted() override;

 private:
  raw_ptr<libstorage::Platform> platform_;
  raw_ptr<StartupDep> startup_dep_;
  const base::FilePath root_;
  const base::FilePath stateful_;
};

}  // namespace startup

#endif  // INIT_STARTUP_MOUNT_VAR_HOME_IMPL_H_
