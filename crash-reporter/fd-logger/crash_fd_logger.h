// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_FD_LOGGER_CRASH_FD_LOGGER_H_
#define CRASH_REPORTER_FD_LOGGER_CRASH_FD_LOGGER_H_

#include <base/files/file_path.h>

namespace fd_logger {

inline constexpr char kDefaultProcPath[] = "/proc";

// For b/207716926, log processes in the system using many file descriptors to
// help identify a potential leak. `proc_path` may be overwritten for testing.
void LogOpenFilesInSystem(
    const base::FilePath& proc_path = base::FilePath(kDefaultProcPath));

}  // namespace fd_logger

#endif  // CRASH_REPORTER_FD_LOGGER_CRASH_FD_LOGGER_H_
