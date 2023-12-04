// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_LOCKDOWN_KERNEL_LOCKDOWN_UTILS_H_
#define LIBBRILLO_BRILLO_LOCKDOWN_KERNEL_LOCKDOWN_UTILS_H_

#include <optional>

#include <base/files/file_path.h>
#include <brillo/brillo_export.h>

namespace brillo {

enum class BRILLO_EXPORT KernelLockdownMode {
  kDisabled,
  kIntegrity,
  kConfidentiality
};

inline constexpr char kKernelLockdown[] = "/sys/kernel/security/lockdown";

// Returns the KernelLockdownMode found in kernel_lockdown,
// defaulting to /sys/kernel/security/lockdown.
//
// The contents of kernel_lockdown should match the format
// detailed in linux/security/lockdown/lockdown.c, e.g:
//
// "none [integrity] confidentiality"
//
// If kernel_lockdown cannot be read or does not contain a valid
// lockdown mode, returns nullopt.
// Else, returns respective std::optional<KernelLockdownMode>.
BRILLO_EXPORT std::optional<KernelLockdownMode> GetLockdownMode(
    const base::FilePath& kernel_lockdown = base::FilePath(kKernelLockdown));

}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_LOCKDOWN_KERNEL_LOCKDOWN_UTILS_H_
