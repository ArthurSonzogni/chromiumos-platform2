// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_UPDATER_UPDATE_UTILS_H_
#define BIOD_UPDATER_UPDATE_UTILS_H_

#include <sys/types.h>
#include <unistd.h>

#include <optional>
#include <string>

#include <base/files/file_path.h>
#include <base/files/scoped_file.h>
#include <brillo/files/safe_fd.h>

#include "biod/biod_system.h"

namespace biod {
namespace updater {

std::string UpdaterVersion();

// Checks for external firmware disable mechanism.
bool UpdateDisallowed(const BiodSystem& system);

// Opens the existing directory |dir_path| without following symlinks in any
// path component. Returns std::nullopt on failure.
std::optional<brillo::SafeFD> OpenDirectorySafely(
    const base::FilePath& dir_path);

// Opens the regular file |file_name| inside |dir_fd| for appending, creating it
// with |permissions| and owned by the effective uid/gid if necessary. Symlinks
// are never followed; an existing entry that is not a regular file with the
// expected ownership is replaced. Returns std::nullopt on failure.
std::optional<brillo::SafeFD> OpenFileForWritingSafely(
    brillo::SafeFD& dir_fd, const std::string& file_name, mode_t permissions);

// Converts |fd| into a FILE* stream opened with |mode|. The returned stream
// owns a duplicate of the underlying descriptor, so |fd| can be closed
// independently. Returns nullptr on failure.
base::ScopedFILE SafeFDToScopedFILE(brillo::SafeFD fd, const char* mode);

// Rotates |latest_symlink| to |previous_symlink| and creates a new relative
// |latest_symlink| pointing to |log_file_name|, all inside |dir_fd| and without
// following symlinks. Any existing entry at |previous_symlink| is removed, and
// |latest_symlink| is only rotated if it is a symlink owned by |expected_uid|
// (otherwise it is removed). All names must be single path components. Safe to
// use as root in a directory writable by an unprivileged user, unlike
// brillo::UpdateLogSymlinks().
void UpdateLogSymlinksSafely(brillo::SafeFD& dir_fd,
                             const std::string& latest_symlink,
                             const std::string& previous_symlink,
                             const std::string& log_file_name,
                             uid_t expected_uid = geteuid());

// Initializes logging to a timestamped bio_fw_updater log file in
// |log_dir_path| and updates the bio_fw_updater.LATEST and
// bio_fw_updater.PREVIOUS symlinks. If |log_dir_path| does not exist or file
// logging cannot be initialized, falls back to syslog and stderr logging and
// returns false.
bool SetupLogging(const base::FilePath& log_dir_path);

}  // namespace updater
}  // namespace biod

#endif  // BIOD_UPDATER_UPDATE_UTILS_H_
