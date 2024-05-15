// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "brillo/userdb_utils.h"

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/posix/safe_strerror.h>

namespace brillo {
namespace userdb {

constexpr ssize_t kBufLen = 16384;

bool GetUserInfo(const std::string& user, uid_t* uid, gid_t* gid) {
  ssize_t buf_len = sysconf(_SC_GETPW_R_SIZE_MAX);
  if (buf_len < 0)
    buf_len = 16384;  // 16K should be enough?...
  passwd pwd_buf;
  passwd* pwd = nullptr;
  std::vector<char> buf(buf_len);

  int err_num;
  do {
    err_num = getpwnam_r(user.c_str(), &pwd_buf, buf.data(), buf_len, &pwd);
  } while (err_num == EINTR);

  if (!pwd) {
    LOG(ERROR) << "Unable to find user " << user << ": "
               << (err_num ? base::safe_strerror(err_num)
                           : "No matching record");
    return false;
  }

  if (uid)
    *uid = pwd->pw_uid;
  if (gid)
    *gid = pwd->pw_gid;
  return true;
}

bool GetGroupInfo(const std::string& group, gid_t* gid) {
  ssize_t buf_len = sysconf(_SC_GETGR_R_SIZE_MAX);
  if (buf_len < 0)
    buf_len = 16384;  // 16K should be enough?...
  struct group grp_buf;
  struct group* grp = nullptr;
  std::vector<char> buf(buf_len);

  int err_num;
  do {
    err_num = getgrnam_r(group.c_str(), &grp_buf, buf.data(), buf_len, &grp);
  } while (err_num == EINTR);

  if (!grp) {
    LOG(ERROR) << "Unable to find group " << group << ": "
               << (err_num ? base::safe_strerror(err_num)
                           : "No matching record");
    return false;
  }

  if (gid)
    *gid = grp->gr_gid;
  return true;
}

std::vector<uid_t> GetUsers(const base::FilePath& path) {
  std::vector<uid_t> accts;
  struct passwd pw;
  struct passwd* pwres;
  char buf[kBufLen];
  int res;
  base::FilePath acctPath = path;

  if (acctPath.empty()) {
    acctPath = base::FilePath("/etc/passwd");
  }
  base::ScopedFILE acctFile(fopen(acctPath.value().c_str(), "re"));

  while ((res = fgetpwent_r(acctFile.get(), &pw, buf, sizeof(buf), &pwres)) ==
             0 &&
         pwres != nullptr) {
    accts.push_back(pw.pw_uid);
  }

  return accts;
}

std::vector<uid_t> GetUsers() {
  return GetUsers(base::FilePath());
}

std::vector<uid_t> GetGroups(const base::FilePath& path) {
  std::vector<gid_t> accts;
  struct group grp;
  struct group* grpres;
  char buf[kBufLen];
  int res;
  base::FilePath acctPath = path;

  if (acctPath.empty()) {
    acctPath = base::FilePath("/etc/group");
  }
  base::ScopedFILE acctFile(fopen(acctPath.value().c_str(), "re"));

  while ((res = fgetgrent_r(acctFile.get(), &grp, buf, sizeof(buf), &grpres)) ==
             0 &&
         grpres != nullptr) {
    accts.push_back(grp.gr_gid);
  }

  return accts;
}

std::vector<gid_t> GetGroups() {
  return GetGroups(base::FilePath());
}

}  // namespace userdb
}  // namespace brillo
