// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlp/file_id.h"

#include <fcntl.h>
#include <string>
#include <sys/stat.h>

#include <base/logging.h>
#include <base/time/time.h>

namespace dlp {

namespace {
// Converts a statx_timestamp struct to time_t.
time_t ConvertStatxTimestampToTimeT(const struct statx_timestamp& sts) {
  struct timespec ts;
  ts.tv_sec = sts.tv_sec;
  ts.tv_nsec = sts.tv_nsec;
  return base::Time::FromTimeSpec(ts).ToTimeT();
}

}  // namespace

FileId GetFileId(const std::string& path) {
  struct statx file_statx;
  if (statx(AT_FDCWD, path.c_str(), AT_STATX_SYNC_AS_STAT,
            STATX_INO | STATX_BTIME, &file_statx) != 0) {
    PLOG(ERROR) << "Could not access " << path;
    return {0, 0};
  }
  if (!(file_statx.stx_mask & STATX_BTIME) ||
      !(file_statx.stx_mask & STATX_INO)) {
    PLOG(ERROR) << "statx failed";
    return {0, 0};
  }
  return std::make_pair(file_statx.stx_ino,
                        ConvertStatxTimestampToTimeT(file_statx.stx_btime));
}

}  // namespace dlp
