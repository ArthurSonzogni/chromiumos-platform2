// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "installer/chromeos_install.h"

int main(int argc, char* argv[]) {
  return installer::ChromeOsInstall().Run(argv);
}
