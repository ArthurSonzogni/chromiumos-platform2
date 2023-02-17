// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRINTSCANMGR_DAEMON_UTILS_HELPER_UTILS_H_
#define PRINTSCANMGR_DAEMON_UTILS_HELPER_UTILS_H_

#include <string>

namespace printscanmgr {

// Get the full path of a helper executable located at the |relative_path|
// relative to the debugd helpers directory. Return false if the full path
// is too long.
bool GetHelperPath(const std::string& relative_path, std::string* full_path);

}  // namespace printscanmgr

#endif  // PRINTSCANMGR_DAEMON_UTILS_HELPER_UTILS_H_
