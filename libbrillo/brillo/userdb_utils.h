// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_USERDB_UTILS_H_
#define LIBBRILLO_BRILLO_USERDB_UTILS_H_

#include <sys/types.h>

#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <brillo/brillo_export.h>

namespace brillo {
namespace userdb {

// Looks up the UID and GID corresponding to |user|. Returns true on success.
// Passing nullptr for |uid| or |gid| causes them to be ignored.
[[nodiscard]] BRILLO_EXPORT bool GetUserInfo(const std::string& user,
                                             uid_t* uid,
                                             gid_t* gid);

// Looks up the GID corresponding to |group|. Returns true on success.
// Passing nullptr for |gid| causes it to be ignored.
[[nodiscard]] BRILLO_EXPORT bool GetGroupInfo(const std::string& group,
                                              gid_t* gid);

// Returns a vector of all UIDs found on the system.
[[nodiscard]] BRILLO_EXPORT std::vector<uid_t> GetUsers(
    const base::FilePath& path);

[[nodiscard]] BRILLO_EXPORT std::vector<uid_t> GetUsers();

// Returns a vector of all GIDs found on the system.
[[nodiscard]] BRILLO_EXPORT std::vector<gid_t> GetGroups(
    const base::FilePath& path);

[[nodiscard]] BRILLO_EXPORT std::vector<gid_t> GetGroups();

}  // namespace userdb
}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_USERDB_UTILS_H_
