// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include "installer/chromeos_install.h"

namespace installer {

namespace {

constexpr char kChromeOsInstallScript[] = "chromeos-install.sh";

}  // namespace

int ChromeOsInstall::Run(char* const argv[]) {
  return execvp(kChromeOsInstallScript, argv);
}

}  // namespace installer
