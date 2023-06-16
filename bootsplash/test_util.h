// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BOOTSPLASH_TEST_UTIL_H_
#define BOOTSPLASH_TEST_UTIL_H_

#include <string>

namespace bootsplash {

// Get a path, potentially modified by a sysroot for testing.
std::string GetPath(const std::string& path);

// Set the sysroot for the purposes of testing.
void SetSysrootForTesting(const std::string& sysroot);

}  // namespace bootsplash

#endif  // BOOTSPLASH_TEST_UTIL_H_
