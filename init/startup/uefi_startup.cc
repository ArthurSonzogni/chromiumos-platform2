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
#include "init/utils.h"

namespace startup {

std::unique_ptr<UefiDelegate> UefiDelegate::Create(
    libstorage::Platform* platform, const base::FilePath& root_dir) {
  return std::make_unique<UefiDelegateImpl>(platform, root_dir);
}

UefiDelegate::~UefiDelegate() = default;

UefiDelegateImpl::UefiDelegateImpl(libstorage::Platform* platform,
                                   const base::FilePath& root_dir)
    : platform_(platform), root_dir_(root_dir) {}

bool UefiDelegateImpl::IsUefiEnabled() const {
  return platform_->DirectoryExists(root_dir_.Append(kSysEfiDir));
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

bool UefiDelegateImpl::MountEfivarfs(const UserAndGroup& fwupd) {
  const base::FilePath efivars_dir = root_dir_.Append(kEfivarsDir);
  const std::string data =
      "uid=" + std::to_string(fwupd.uid) + ",gid=" + std::to_string(fwupd.gid);
  if (!platform_->Mount(/*from=*/base::FilePath(),
                        /*to=*/efivars_dir,
                        /*type=*/kFsTypeEfivarfs,
                        /*mount_flags=*/kCommonMountFlags,
                        /*mount_options=*/data)) {
    PLOG(WARNING) << "Unable to mount " << efivars_dir;
    return false;
  }

  return true;
}

bool UefiDelegateImpl::MakeUefiVarMutable(const std::string& vendor,
                                          const std::string& name) {
  const base::FilePath var_path =
      root_dir_.Append(kEfivarsDir).Append(name + '-' + vendor);
  return platform_->SetExtFileAttributes(var_path, 0, FS_IMMUTABLE_FL);
}

void UefiDelegateImpl::MakeEsrtReadableByFwupd(const UserAndGroup& fwupd) {
  const base::FilePath esrt_dir = root_dir_.Append(kSysEfiDir).Append("esrt");
  std::unique_ptr<libstorage::FileEnumerator> file_enumerator(
      platform_->GetFileEnumerator(
          esrt_dir, /*recursive=*/true,
          base::FileEnumerator::DIRECTORIES | base::FileEnumerator::FILES));

  for (base::FilePath path = file_enumerator->Next(); !path.empty();
       path = file_enumerator->Next()) {
    if (!platform_->SetOwnership(path, fwupd.uid, fwupd.gid,
                                 false /* follow_links */)) {
      PLOG(WARNING) << "Failed to change ownership of " << path << " to "
                    << fwupd.uid << ":" << fwupd.gid;
    }
  }
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

  if (uefi_delegate.MountEfivarfs(fwupd.value())) {
    uefi_delegate.MakeUefiVarMutable(kEfiImageSecurityDatabaseGuid, "dbx");
  }

  uefi_delegate.MakeEsrtReadableByFwupd(fwupd.value());
}

}  // namespace startup
