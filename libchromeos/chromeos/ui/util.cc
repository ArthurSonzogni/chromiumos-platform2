// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/ui/util.h"

#include <pwd.h>
#include <unistd.h>

#include <vector>

#include <base/command_line.h>
#include <base/file_util.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/process/launch.h>

namespace chromeos {
namespace ui {
namespace util {

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
  if (getuid() == 0) {
    if (chown(path.value().c_str(), uid, gid) != 0) {
      PLOG(ERROR) << "Couldn't chown " << path.value() << " to "
                  << uid << ":" << gid;
      return false;
    }
  }
  if (chmod(path.value().c_str(), mode) != 0) {
    PLOG(ERROR) << "Unable to chmod " << path.value() << " to "
                << std::oct << mode;
    return false;
  }
  return true;
}

bool EnsureDirectoryExists(const base::FilePath& path,
                           uid_t uid,
                           gid_t gid,
                           mode_t mode) {
  if (!base::CreateDirectory(path)) {
    PLOG(ERROR) << "Unable to create " << path.value();
    return false;
  }
  return SetPermissions(path, uid, gid, mode);
}

bool GetUserInfo(const std::string& user, uid_t* uid, gid_t* gid) {
  ssize_t buf_len = sysconf(_SC_GETPW_R_SIZE_MAX);
  if (buf_len < 0)
    buf_len = 16384;  // 16K should be enough?...
  passwd pwd_buf;
  passwd* pwd = NULL;
  std::vector<char> buf(buf_len);
  if (getpwnam_r(user.c_str(), &pwd_buf, buf.data(), buf_len, &pwd) || !pwd) {
    PLOG(ERROR) << "Unable to find user " << user;
    return false;
  }

  if (uid)
    *uid = pwd->pw_uid;
  if (gid)
    *gid = pwd->pw_gid;
  return true;
}

bool Run(const char* command,
         const char* arg, ...) {
  // Extra parentheses because yay C++ most vexing parse.
  base::CommandLine cl((base::FilePath(command)));
  va_list list;
  va_start(list, arg);
  while (arg) {
    cl.AppendArg(const_cast<char*>(arg));
    arg = va_arg(list, char*);
  }
  va_end(list);

  std::string output;
  int exit_code = 0;
  if (!base::GetAppOutputWithExitCode(cl, &output, &exit_code)) {
    LOG(WARNING) << "\"" << cl.GetCommandLineString() << "\" failed with "
                 << exit_code << ": " << output;
    return false;
  }

  return true;
}

}  // namespace util
}  // namespace ui
}  // namespace chromeos
