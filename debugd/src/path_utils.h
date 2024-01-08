// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_PATH_UTILS_H_
#define DEBUGD_SRC_PATH_UTILS_H_

#include <string_view>

#include <base/files/file_path.h>

namespace debugd {
namespace path_utils {

// Gets a FilePath from the given path. A prefix will be added if the prefix is
// set with |SetPrefixForTesting()|.
base::FilePath GetFilePath(std::string_view file_path);

namespace testing {

// Sets a prefix that'll be added when |GetFilePath()| is called, for testing.
// For example, if "/tmp" is set as the prefix, GetFilePath("/sys/foo") will
// return "/tmp/sys/foo". Passing an empty path (|base::FilePath()|) will reset
// the prefix. The caller will be responsible to reset the prefix after use.
void SetPrefixForTesting(const base::FilePath& prefix);

}  // namespace testing
}  // namespace path_utils
}  // namespace debugd

#endif  // DEBUGD_SRC_PATH_UTILS_H_
