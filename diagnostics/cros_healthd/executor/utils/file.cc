// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/executor/utils/file.h"

#include <base/check.h>
#include <base/logging.h>
#include <base/time/time.h>
#include <sys/stat.h>
#include <sys/time.h>

namespace diagnostics {

namespace {
// Converts a statx_timestamp struct to `base::Time`.
base::Time ConvertStatxTimestampToTime(const struct statx_timestamp& sts) {
  struct timespec ts;
  ts.tv_sec = sts.tv_sec;
  ts.tv_nsec = sts.tv_nsec;
  return base::Time::FromTimeSpec(ts);
}
}  // namespace

bool GetCreationTime(const base::FilePath& file_path, base::Time& out) {
  CHECK(file_path.IsAbsolute())
      << "File name in GetCreationTime must be absolute";
  struct statx statx_result;
  if (statx(/*dirfd=ignored*/ 0, file_path.value().c_str(), /*flags=*/0,
            /*masks=*/STATX_BTIME, &statx_result) != 0) {
    PLOG(ERROR) << "statx failed for file " << file_path;
    return false;
  }
  if (!(statx_result.stx_mask & STATX_BTIME)) {
    // Creation time is not obtained even though statx succeeded.
    PLOG(ERROR)
        << "statx failed to obtain creation time even though statx succeeded "
        << file_path;
    return false;
  }

  out = ConvertStatxTimestampToTime(statx_result.stx_btime);
  return true;
}
}  // namespace diagnostics
