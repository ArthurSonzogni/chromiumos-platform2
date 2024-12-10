// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <brillo/syslog_logging.h>

#include "cryptohome/lockbox_cache_manager/lockbox_cache_manager.h"

int main(int argc, char** argv) {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderr);
  cryptohome::LockboxCacheManager cache_manager(base::FilePath("/"));
  bool ok = cache_manager.Run();
  return !ok;
}
