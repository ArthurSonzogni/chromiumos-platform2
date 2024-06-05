// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/startup/standard_mount_helper.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/values.h>
#include <libstorage/platform/platform.h>

#include "init/startup/flags.h"
#include "init/startup/mount_helper.h"
#include "init/startup/startup_dep_impl.h"

namespace startup {

// Constructor for StandardMountHelper when the device is
// not in dev mode.
StandardMountHelper::StandardMountHelper(libstorage::Platform* platform,
                                         StartupDep* startup_dep,
                                         const Flags& flags,
                                         const base::FilePath& root,
                                         const base::FilePath& stateful)
    : MountHelper(platform, startup_dep, flags, root, stateful) {}

bool StandardMountHelper::DoMountVarAndHomeChronos() {
  return MountVarAndHomeChronos();
}

}  // namespace startup
