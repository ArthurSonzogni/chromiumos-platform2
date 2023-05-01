// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_STARTUP_UEFI_STARTUP_IMPL_H_
#define INIT_STARTUP_UEFI_STARTUP_IMPL_H_

#include <base/files/file_path.h>

#include "init/startup/platform_impl.h"
#include "init/startup/uefi_startup.h"

namespace startup {

// Path of the system efi directory (relative to the root dir). This
// directory will only exist when booting from UEFI firmware.
constexpr char kSysEfiDir[] = "sys/firmware/efi";

// Mount point for efivarfs (relative to the root dir). This directory
// is used to read and write UEFI variables.
constexpr char kEfivarsDir[] = "sys/firmware/efi/efivars";

// File system name used for mounting efivarfs.
constexpr char kFsTypeEfivarfs[] = "efivarfs";

class UefiDelegateImpl : public UefiDelegate {
 public:
  UefiDelegateImpl(Platform& platform, const base::FilePath& root_dir);

  bool IsUefiEnabled() const override;
  bool MountEfivarfs() override;

 private:
  Platform& platform_;
  const base::FilePath root_dir_;
};

}  // namespace startup

#endif  // INIT_STARTUP_UEFI_STARTUP_IMPL_H_
