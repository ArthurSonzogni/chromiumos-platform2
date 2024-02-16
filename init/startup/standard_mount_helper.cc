// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/values.h>

#include "init/startup/flags.h"
#include "init/startup/mount_helper.h"
#include "init/startup/startup_dep_impl.h"
#include "init/startup/standard_mount_helper.h"

namespace startup {

// Constructor for StandardMountHelper when the device is
// not in dev mode.
StandardMountHelper::StandardMountHelper(StartupDep* startup_dep,
                                         const startup::Flags& flags,
                                         const base::FilePath& root,
                                         const base::FilePath& stateful,
                                         const bool dev_mode)
    : startup::MountHelper(startup_dep, flags, root, stateful, dev_mode) {}

bool StandardMountHelper::DoMountVarAndHomeChronos() {
  return MountVarAndHomeChronos();
}

startup::MountHelperType StandardMountHelper::GetMountHelperType() const {
  return startup::MountHelperType::kStandardMode;
}

}  // namespace startup
