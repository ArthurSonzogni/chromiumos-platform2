// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>

#include "lorgnette/debug_log.h"

namespace lorgnette {

bool SetupDebugging(const base::FilePath& flagPath) {
  if (!base::PathExists(flagPath)) {
    return false;
  }

  setenv("PFUFS_DEBUG", "1", 1);
  setenv("SANE_DEBUG_AIRSCAN", "16", 1);
  setenv("SANE_DEBUG_EPSONDS", "16", 1);
  setenv("SANE_DEBUG_EPSON2", "16", 1);
  setenv("SANE_DEBUG_FUJITSU", "20", 1);
  setenv("SANE_DEBUG_PIXMA", "4", 1);

  return true;
}

}  // namespace lorgnette
