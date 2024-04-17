// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/startup/mount_var_home_unencrypted_impl.h"

#include <sys/mount.h>

#include <memory>
#include <optional>
#include <stack>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/threading/platform_thread.h>
#include <base/values.h>
#include <brillo/process/process.h>
#include <libstorage/platform/platform.h>

#include "init/startup/mount_helper.h"
#include "init/startup/startup_dep_impl.h"

namespace {

constexpr char kVar[] = "var";
constexpr char kHomeChronos[] = "home/chronos";

}  // namespace

namespace startup {

MountVarAndHomeChronosUnencryptedImpl::MountVarAndHomeChronosUnencryptedImpl(
    libstorage::Platform* platform,
    StartupDep* startup_dep,
    const base::FilePath& root,
    const base::FilePath& stateful)
    : platform_(platform),
      startup_dep_(startup_dep),
      root_(root),
      stateful_(stateful) {}

// Bind mount /var and /home/chronos. All function arguments are ignored.
bool MountVarAndHomeChronosUnencryptedImpl::Mount(
    std::optional<encryption::EncryptionKey> _) {
  base::FilePath var = stateful_.Append(kVar);
  if (!platform_->CreateDirectory(var)) {
    return false;
  }

  if (!platform_->SetPermissions(var, 0755)) {
    PLOG(WARNING) << "chmod failed for " << var.value();
    return false;
  }

  if (!platform_->Mount(var, root_.Append(kVar), "", MS_BIND, "")) {
    return false;
  }
  if (!platform_->Mount(stateful_.Append(kHomeChronos),
                        root_.Append(kHomeChronos), "", MS_BIND, "")) {
    platform_->Unmount(root_.Append(kVar), false, nullptr);
    return false;
  }
  return true;
}

// Unmount bind mounts for /var and /home/chronos.
bool MountVarAndHomeChronosUnencryptedImpl::Umount() {
  bool ret = false;
  if (platform_->Unmount(root_.Append(kVar), false, nullptr)) {
    ret = true;
  }
  if (platform_->Unmount(root_.Append(kHomeChronos), false, nullptr)) {
    ret = true;
  }
  return ret;
}

}  // namespace startup
