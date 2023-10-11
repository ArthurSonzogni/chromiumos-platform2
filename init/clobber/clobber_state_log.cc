// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/clobber/clobber_state_log.h"

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>

namespace init {

void AppendToLog(const std::string_view& source, const std::string& contents) {
  if (!base::AppendToFile(base::FilePath(kClobberLogPath), contents)) {
    PLOG(ERROR) << "Appending " << source << " to clobber-state log failed";
  }
}

}  // namespace init
