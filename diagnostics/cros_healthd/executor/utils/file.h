// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_FILE_H_
#define DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_FILE_H_

#include <base/files/file_path.h>
#include <base/time/time.h>

namespace diagnostics {
// Gets the creation time of a given file. The file path must be absolute.
//
// `base::File::GetInfo` is supposed to return file creation time. However, the
// returned file creation time is the last inode status update time instead of
// the actual creation time, which would be affected even by file size changes.
// See
// https://source.chromium.org/chromium/chromiumos/platform/libchrome/+/main:base/files/file_posix.cc;l=136-142;drc=e00272d96efa9f3778c5f4cae09dea56fa4729b8
// and https://man7.org/linux/man-pages/man7/inode.7.html.
//
// TODO(crbug/1442014): Migrate to calls to this function to
// base::File::GetInfo() once this issue is resolved.
bool GetCreationTime(const base::FilePath& file_path, base::Time& out);
}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_FILE_H_
