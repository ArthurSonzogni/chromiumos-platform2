// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_COMMON_UTIL_H_
#define POWER_MANAGER_COMMON_UTIL_H_

#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/time/time.h>

namespace power_manager {
namespace util {

// Clamps |percent| in the range [0.0, 100.0].
double ClampPercent(double percent);

// Returns |delta| as a string of the format "4h3m45s".
std::string TimeDeltaToString(base::TimeDelta delta);

// Returns a list of paths to pass when creating a Prefs object. For a given
// preference, |read_write_path| will be checked first, then the board-specific
// subdirectory within |read_only_path|, and finally |read_only_path|.
std::vector<base::FilePath> GetPrefPaths(const base::FilePath& read_write_path,
                                         const base::FilePath& read_only_path);

// Writes the given buffer into the file, overwriting any data that was
// previously there.  Returns true if all bytes are written or false otherwise.
bool WriteFileFully(const base::FilePath& filename, const char* data, int size);

}  // namespace util
}  // namespace power_manager

#endif  // POWER_MANAGER_COMMON_UTIL_H_
