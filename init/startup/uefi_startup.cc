// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/startup/uefi_startup.h"

#include <fcntl.h>
#include <sys/mount.h>

// The include for sys/mount.h must come before this.
#include <linux/fs.h>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/userdb_utils.h>

#include "init/startup/constants.h"
#include "init/startup/uefi_startup_impl.h"

namespace startup {

std::unique_ptr<UefiDelegate> UefiDelegate::Create(
    Platform& platform, const base::FilePath& root_dir) {
  return std::make_unique<UefiDelegateImpl>(platform, root_dir);
}

UefiDelegate::~UefiDelegate() = default;

UefiDelegateImpl::UefiDelegateImpl(Platform& platform,
                                   const base::FilePath& root_dir)
    : platform_(platform), root_dir_(root_dir) {}

bool UefiDelegateImpl::IsUefiEnabled() const {
  return base::PathExists(root_dir_.Append(kSysEfiDir));
}

std::optional<UefiDelegate::UserAndGroup>
UefiDelegateImpl::GetFwupdUserAndGroup() const {
  UserAndGroup fwupd;

  if (!brillo::userdb::GetUserInfo("fwupd", &fwupd.uid, nullptr)) {
    return std::nullopt;
  }

  if (!brillo::userdb::GetGroupInfo("fwupd", &fwupd.gid)) {
    return std::nullopt;
  }

  return fwupd;
}

bool UefiDelegateImpl::MountEfivarfs() {
  const base::FilePath efivars_dir = root_dir_.Append(kEfivarsDir);

  if (!platform_.Mount(/*src=*/kFsTypeEfivarfs,
                       /*dst=*/efivars_dir,
                       /*type=*/kFsTypeEfivarfs,
                       /*flags=*/kCommonMountFlags,
                       /*data=*/"")) {
    PLOG(WARNING) << "Unable to mount " << efivars_dir;
    return false;
  }

  return true;
}

bool UefiDelegateImpl::MakeUefiVarWritableByFwupd(const std::string& vendor,
                                                  const std::string& name,
                                                  const UserAndGroup& fwupd) {
  const base::FilePath var_path =
      root_dir_.Append(kEfivarsDir).Append(name + '-' + vendor);
  base::ScopedFD var_fd = platform_.Open(var_path, O_RDONLY | O_CLOEXEC);
  if (!var_fd.is_valid()) {
    PLOG(WARNING) << "Failed to open " << var_path;
    return false;
  }

  int attr = 0;
  if (platform_.Ioctl(var_fd.get(), FS_IOC_GETFLAGS, &attr) < 0) {
    PLOG(WARNING) << "Failed to get attributes for " << var_path;
    return false;
  }
  attr &= ~FS_IMMUTABLE_FL;
  if (platform_.Ioctl(var_fd.get(), FS_IOC_SETFLAGS, &attr) < 0) {
    PLOG(WARNING) << "Failed to set attributes for " << var_path;
    return false;
  }

  if (!platform_.Fchown(var_fd.get(), fwupd.uid, fwupd.gid)) {
    PLOG(WARNING) << "Failed to change ownership of " << var_path << " to "
                  << fwupd.uid << ":" << fwupd.gid;
    return false;
  }

  return true;
}

void MaybeRunUefiStartup(UefiDelegate& uefi_delegate) {
  if (!uefi_delegate.IsUefiEnabled()) {
    return;
  }

  const auto fwupd = uefi_delegate.GetFwupdUserAndGroup();
  if (!fwupd.has_value()) {
    LOG(WARNING) << "Failed to get fwupd user or group";
    return;
  }

  if (uefi_delegate.MountEfivarfs()) {
    uefi_delegate.MakeUefiVarWritableByFwupd(kEfiImageSecurityDatabaseGuid,
                                             "dbx", fwupd.value());
  }
}

}  // namespace startup
