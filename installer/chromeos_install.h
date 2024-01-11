// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INSTALLER_CHROMEOS_INSTALL_H_
#define INSTALLER_CHROMEOS_INSTALL_H_

namespace installer {

// This class provides the implementation to install the booted OS to a
// destination device.
// Along with other features related to installation.
//
// The historical `chromeos-install` script's features are now managed within
// this class.
class ChromeOsInstall {
 public:
  ChromeOsInstall() = default;
  virtual ~ChromeOsInstall() = default;

  // Not Copyable.
  ChromeOsInstall(const ChromeOsInstall&) = delete;
  ChromeOsInstall& operator=(const ChromeOsInstall&) = delete;

  // Will run the `chromeos-install[.sh]` script, only returns on error.
  int Run(char* const argv[]);
};

}  // namespace installer

#endif  // INSTALLER_CHROMEOS_INSTALL_H_
