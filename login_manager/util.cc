// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/util.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <vector>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>

namespace chromeos::ui::util {

base::FilePath GetReparentedPath(const std::string& path,
                                 const base::FilePath& parent) {
  if (parent.empty())
    return base::FilePath(path);

  CHECK(!path.empty() && path[0] == '/');
  base::FilePath relative_path(path.substr(1));
  CHECK(!relative_path.IsAbsolute());
  return parent.Append(relative_path);
}

bool SetPermissions(const base::FilePath& path,
                    uid_t uid,
                    gid_t gid,
                    mode_t mode) {
  base::ScopedFD fd(
      open(path.value().c_str(), O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "Couldn't open " << path.value();
    return false;
  }

  if (getuid() == 0) {
    if (fchown(fd.get(), uid, gid) != 0) {
      PLOG(ERROR) << "Couldn't chown " << path.value() << " to " << uid << ":"
                  << gid;
      return false;
    }
  }
  if (fchmod(fd.get(), mode) != 0) {
    PLOG(ERROR) << "Unable to chmod " << path.value() << " to " << std::oct
                << mode;
    return false;
  }
  return true;
}

bool EnsureDirectoryExists(const base::FilePath& path,
                           uid_t uid,
                           gid_t gid,
                           mode_t mode) {
  if (!base::DirectoryExists(path)) {
    // Remove the existing file or link if any.
    if (!base::DeleteFile(path)) {
      PLOG(ERROR) << "Unable to delete " << path.value();
      return false;
    }
    if (!base::CreateDirectory(path)) {
      PLOG(ERROR) << "Unable to create " << path.value();
      return false;
    }
  }
  return SetPermissions(path, uid, gid, mode);
}

}  // namespace chromeos::ui::util
