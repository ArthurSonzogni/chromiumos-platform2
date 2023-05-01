// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/startup/uefi_startup.h"

#include <base/files/file_util.h>
#include <base/logging.h>

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

void MaybeRunUefiStartup(UefiDelegate& uefi_delegate) {
  if (!uefi_delegate.IsUefiEnabled()) {
    return;
  }

  uefi_delegate.MountEfivarfs();
}

}  // namespace startup
