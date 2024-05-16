// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/mount.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/values.h>
#include <libstorage/platform/platform.h>

#include "init/startup/factory_mode_mount_helper.h"
#include "init/startup/flags.h"
#include "init/startup/mount_helper.h"
#include "init/startup/startup_dep_impl.h"

namespace {

constexpr char kOptionsFile[] =
    "dev_image/factory/init/encstateful_mount_option";
constexpr char kVar[] = "var";
constexpr char kHomeChronos[] = "home/chronos";

}  // namespace

namespace startup {

// Constructor for FactoryModeMountHelper when the device is
// in factory mode.
FactoryModeMountHelper::FactoryModeMountHelper(libstorage::Platform* platform,
                                               StartupDep* startup_dep,
                                               const Flags& flags,
                                               const base::FilePath& root,
                                               const base::FilePath& stateful,
                                               const bool dev_mode)
    : MountHelper(platform, startup_dep, flags, root, stateful, dev_mode) {}

bool FactoryModeMountHelper::DoMountVarAndHomeChronos() {
  base::FilePath option_file = stateful_.Append(kOptionsFile);
  std::string option = "";
  if (platform_->FileExists(option_file)) {
    platform_->ReadFileToString(option_file, &option);
  }
  if (option == "tmpfs") {
    // Mount tmpfs to /var/. When booting from USB disk, writing to /var/
    // slows down system performance dramatically. Since there is no need to
    // really write to stateful partition, using option 'tmpfs' will mount
    // tmpfs on /var to improve performance. (especially when running
    // tests like touchpad, touchscreen).
    base::FilePath var = root_.Append(kVar);
    if (!platform_->Mount(base::FilePath("tmpfs_var"), var, "tmpfs", 0, "")) {
      return false;
    }
    base::FilePath stateful_home_chronos = stateful_.Append(kHomeChronos);
    base::FilePath home_chronos = root_.Append(kHomeChronos);
    if (!platform_->Mount(stateful_home_chronos, home_chronos, "", MS_BIND,
                          "")) {
      return false;
    }
    return true;
  }
  // Mount /var and /home/chronos in the unencrypted mode.
  return MountVarAndHomeChronosUnencrypted();
}

}  // namespace startup
