// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_UTIL_H_
#define LOGIN_MANAGER_UTIL_H_

#include <string>

#include <base/files/file_path.h>
#include <brillo/brillo_export.h>

namespace chromeos::ui::util {

// Converts an absolute path |path| into a base::FilePath. If |parent| is
// non-empty, |path| is rooted within it. For example, GetPath("/usr/bin/bar",
// base::FilePath("/tmp/foo")) returns base::FilePath("/tmp/foo/usr/bin/bar")).
BRILLO_EXPORT base::FilePath GetReparentedPath(const std::string& path,
                                               const base::FilePath& parent);

}  // namespace chromeos::ui::util

#endif  // LOGIN_MANAGER_UTIL_H_
