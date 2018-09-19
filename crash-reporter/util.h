// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_UTIL_H_
#define CRASH_REPORTER_UTIL_H_

#include <string>
#include <vector>

#include <base/files/file_path.h>

namespace util {

// Returns true if integration tests are currently running.
bool IsCrashTestInProgress();

// Returns true if running on a developer image.
bool IsDeveloperImage();

// Returns true if running on a test image.
bool IsTestImage();

// Tries to find |key| in a key-value file named |base_name| in |directories| in
// the specified order, and writes the value to |value|. This function returns
// as soon as the key is found (i.e. if the key is found in the first directory,
// the remaining directories won't be checked). Returns true on success.
bool GetCachedKeyValue(const base::FilePath& base_name,
                       const std::string& key,
                       const std::vector<base::FilePath>& directories,
                       std::string* value);

// Similar to GetCachedKeyValue(), but this version checks the predefined
// default directories.
bool GetCachedKeyValueDefault(const base::FilePath& base_name,
                              const std::string& key,
                              std::string* value);

}  // namespace util

#endif  // CRASH_REPORTER_UTIL_H_
