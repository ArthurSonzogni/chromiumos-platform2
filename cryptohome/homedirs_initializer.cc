// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>
#include <base/logging.h>
#include <brillo/syslog_logging.h>

#include "cryptohome/crypto.h"
#include "cryptohome/homedirs.h"
#include "cryptohome/platform.h"

int main(int argc, char** argv) {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderr);

  // Read the file before we daemonize so it can be deleted as soon as we exit.
  cryptohome::Platform platform;
  cryptohome::Crypto crypto(&platform);
  cryptohome::HomeDirs homedirs;
  if (!homedirs.Init(&platform, &crypto, /*cache=*/nullptr)) {
    LOG(ERROR) << "Cannot initialize home dirs.";
    return 1;
  }
  constexpr char kShadowDir[] = "/home/.shadow";
  if (!platform.RestoreSELinuxContexts(base::FilePath(kShadowDir), true)) {
    LOG(ERROR) << "Failed to restore file contexts";
    return 1;
  }

  return 0;
}
