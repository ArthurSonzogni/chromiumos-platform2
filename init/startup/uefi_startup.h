// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_STARTUP_UEFI_STARTUP_H_
#define INIT_STARTUP_UEFI_STARTUP_H_

#include <memory>

#include <base/files/file_path.h>

#include "init/startup/platform_impl.h"

namespace startup {

// Abstract class for UEFI operations.
class UefiDelegate {
 public:
  // Create a concrete instance of the default implementation.
  static std::unique_ptr<UefiDelegate> Create(Platform& platform,
                                              const base::FilePath& root_dir);

  virtual ~UefiDelegate();

  // Check if the device was booted from UEFI firmware. This is done by
  // checking if "/sys/firmware/efi" exists.
  virtual bool IsUefiEnabled() const = 0;

  // Mount the filesystem that provides access to UEFI
  // variables. Returns true on success.
  virtual bool MountEfivarfs() = 0;
};

// Initialize directories needed for UEFI platforms. Does nothing if not
// booted from UEFI firmware.
//
// Errors are logged, but not propagated to the caller.
void MaybeRunUefiStartup(UefiDelegate& uefi_delegate);

}  // namespace startup

#endif  // INIT_STARTUP_UEFI_STARTUP_H_
