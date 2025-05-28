// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INSTALLER_CHROMEOS_LEGACY_H_
#define INSTALLER_CHROMEOS_LEGACY_H_

struct InstallConfig;
class Platform;

// Run non-chromebook postinstall, with the particular actions taken
// depending on `install_config.bios_type`.
//
// An error will be returned if `bios_type` is `kUnknown` (i.e. not
// properly initialized) or `kSecure` (i.e. a Chromebook).
//
// Returns true on success, false if any fatal error occurs.
bool RunNonChromebookPostInstall(const Platform& platform,
                                 const InstallConfig& install_config);

#endif  // INSTALLER_CHROMEOS_LEGACY_H_
