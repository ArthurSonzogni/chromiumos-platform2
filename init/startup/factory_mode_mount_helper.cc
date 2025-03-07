// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/startup/factory_mode_mount_helper.h"

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

#include "init/startup/flags.h"
#include "init/startup/mount_helper.h"
#include "init/startup/mount_var_home_interface.h"
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
FactoryModeMountHelper::FactoryModeMountHelper(
    libstorage::Platform* platform,
    StartupDep* startup_dep,
    const Flags* flags,
    const base::FilePath& root,
    const base::FilePath& stateful,
    std::unique_ptr<MountVarAndHomeChronosInterface> impl,
    std::unique_ptr<libstorage::StorageContainerFactory>
        storage_container_factory)
    : MountHelper(platform,
                  startup_dep,
                  flags,
                  root,
                  std::move(impl),
                  std::move(storage_container_factory)),
      stateful_(stateful) {}

bool FactoryModeMountHelper::DoMountVarAndHomeChronos(
    std::optional<encryption::EncryptionKey> _) {
  base::FilePath option_file = stateful_.Append(kOptionsFile);
  std::string option;
  platform_->ReadFileToString(option_file, &option);
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

    base::FilePath chronos = stateful_.Append(kHomeChronos);
    if (!platform_->CreateDirectory(chronos)) {
      return false;
    }
    if (!platform_->SetPermissions(chronos, 0755)) {
      PLOG(WARNING) << "chmod failed for " << chronos.value();
      return false;
    }
    if (!platform_->Mount(chronos, root_.Append(kHomeChronos), "", MS_BIND,
                          "")) {
      return false;
    }
    return true;
  }
  // Mount /var and /home/chronos in the unencrypted mode.
  return MountVarAndHomeChronos(std::nullopt);
}

}  // namespace startup
