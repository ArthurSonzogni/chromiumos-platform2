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

// Some machines, such as some Travelmates, have firmware which
// run into issues getting the boot menu or into BIOS settings
// when the installed ESP install does not have
// an EFI binary at a hardcoded set of locations.
// This function checks the DMI information matching
// machines with these issues.
// The result is used to install a binary `grubx64.efi`
// on the ESP to work around the firmware issues.
// See b:431021440
bool CheckRequiresGrubQuirk(const Platform& platform);

#endif  // INSTALLER_CHROMEOS_LEGACY_H_
