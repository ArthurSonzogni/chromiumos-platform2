// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_STARTUP_STANDARD_MOUNT_HELPER_H_
#define INIT_STARTUP_STANDARD_MOUNT_HELPER_H_

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/values.h>
#include <libstorage/platform/platform.h>

#include "init/startup/flags.h"
#include "init/startup/mount_helper.h"
#include "init/startup/startup_dep_impl.h"

namespace startup {

// This class defines the behavior for when the device is not running a test
// image or in factory mode.
class StandardMountHelper : public MountHelper {
 public:
  explicit StandardMountHelper(libstorage::Platform* platform,
                               StartupDep* startup_dep,
                               const Flags& flags,
                               const base::FilePath& root,
                               const base::FilePath& stateful,
                               const bool dev_mode);

  bool DoMountVarAndHomeChronos() override;
};

}  // namespace startup

#endif  // INIT_STARTUP_STANDARD_MOUNT_HELPER_H_
