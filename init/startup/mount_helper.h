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

#include "init/startup/flags.h"
#include "init/startup/platform_impl.h"

namespace startup {

// MountHelper contains the functionality for maintaining the mount stack
// and the mounting and umounting of /var and /home/chronos.
class MountHelper {
 public:
  MountHelper(std::unique_ptr<Platform> platform,
              const Flags& flags,
              const base::FilePath& root,
              const base::FilePath& stateful);
  virtual ~MountHelper() = default;

  // On failure unmount all saved mount points and repair stateful.
  void CleanupMountsStack(std::vector<base::FilePath>* mnts);
  // Unmounts the incomplete mount setup during the failure path.
  void CleanupMounts(const std::string& msg);
  // Sets up a mount stack for testing.
  void SetMountStackForTest(const std::stack<base::FilePath>& mount_stack);
  // Gets the mount stack for testing.
  std::stack<base::FilePath> GetMountStackForTest() { return mount_stack_; }

  // Unmount bind mounts for /var and /home/chronos when encrypted.
  bool UmountVarAndHomeChronosEncrypted();
  // Unmount bind mounts for /var and /home/chronos when unencrypted.
  bool UmountVarAndHomeChronosUnencrypted();

  Flags GetFlags();

  // Checks for encstateful flag, then calls the appropriate
  // UmountVarAndHomeChronos function.
  bool DoUmountVarAndHomeChronos();

 protected:
  std::unique_ptr<Platform> platform_;
  const startup::Flags flags_;
  const base::FilePath root_;
  const base::FilePath stateful_;
  std::stack<base::FilePath> mount_stack_;
};

}  // namespace startup

#endif  // INIT_STARTUP_MOUNT_HELPER_H_
