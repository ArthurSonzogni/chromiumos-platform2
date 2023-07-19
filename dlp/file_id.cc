// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlp/file_id.h"

#include <string>
#include <sys/stat.h>

#include <base/logging.h>

namespace dlp {

FileId GetFileId(const std::string& path) {
  struct stat file_stats;
  if (stat(path.c_str(), &file_stats) != 0) {
    PLOG(ERROR) << "Could not access " << path;
    return {0, 0};
  }
  return {file_stats.st_ino, /*crtime=*/0};
}

}  // namespace dlp
