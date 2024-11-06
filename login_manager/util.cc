// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/util.h"

#include <base/check.h>
#include <base/files/file_path.h>

namespace chromeos::ui::util {

base::FilePath GetReparentedPath(const std::string& path,
                                 const base::FilePath& parent) {
  if (parent.empty()) {
    return base::FilePath(path);
  }

  CHECK(!path.empty() && path[0] == '/');
  base::FilePath relative_path(path.substr(1));
  CHECK(!relative_path.IsAbsolute());
  return parent.Append(relative_path);
}

}  // namespace chromeos::ui::util
