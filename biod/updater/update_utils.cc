// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/updater/update_utils.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/logging/logging_settings.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <brillo/daemons/daemon.h>
#include <brillo/files/file_util.h>
#include <brillo/files/safe_fd.h>
#include <brillo/syslog_logging.h>
#include <brillo/vcsid.h>

#include "biod/biod_config.h"

namespace {

constexpr char kUpdateDisableFile[] =
    "/var/lib/bio_fw_updater/.disable_fp_updater";
constexpr char kLatestLogSymlink[] = "bio_fw_updater.LATEST";
constexpr char kPreviousLogSymlink[] = "bio_fw_updater.PREVIOUS";

bool LogSetupDirectory(const base::FilePath& log_dir_path) {
  std::optional<brillo::SafeFD> log_dir_fd =
      biod::updater::OpenDirectorySafely(log_dir_path);
  if (!log_dir_fd.has_value()) {
    return false;
  }

  const std::string log_file_name =
      base::StringPrintf("bio_fw_updater.%s",
                         brillo::GetTimeAsLogString(base::Time::Now()).c_str());

  constexpr mode_t kLogFilePermissions = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
  std::optional<brillo::SafeFD> log_fd =
      biod::updater::OpenFileForWritingSafely(*log_dir_fd, log_file_name,
                                              kLogFilePermissions);
  if (!log_fd.has_value()) {
    return false;
  }

  // logging::LoggingSettings only accepts an open log handle as a FILE* (which
  // it takes ownership of and closes via fclose()), whereas passing
  // log_file_path would reopen the file by path without O_NOFOLLOW. Moreover,
  // brillo::SafeFD has no release() method and closes its descriptor on
  // destruction, so we cannot pass its FD directly to fdopen().
  base::ScopedFILE log_stream =
      biod::updater::SafeFDToScopedFILE(std::move(*log_fd), "a");
  if (!log_stream) {
    return false;
  }

  logging::LoggingSettings logging_settings;
  logging_settings.logging_dest = logging::LOG_TO_FILE;
  logging_settings.log_file = log_stream.release();
  logging_settings.lock_log = logging::DONT_LOCK_LOG_FILE;
  if (!logging::InitLogging(logging_settings)) {
    return false;
  }
  logging::SetLogItems(true,    // process ID
                       true,    // thread ID
                       true,    // timestamp
                       false);  // tickcount

  // Update the symlinks only once the log file exists, so that
  // bio_fw_updater.LATEST never points to a missing file and symlink errors
  // are recorded in the log file.
  biod::updater::UpdateLogSymlinksSafely(*log_dir_fd, kLatestLogSymlink,
                                         kPreviousLogSymlink, log_file_name);
  return true;
}

}  // namespace

namespace biod {
namespace updater {

std::string UpdaterVersion() {
  static_assert(brillo::kVCSID,
                "The updater requires VCSID to function properly.");
  return std::string(*brillo::kVCSID);
}

bool UpdateDisallowed(const BiodSystem& system) {
  // Disable updates when /var/lib/bio_fw_updater/.disable_fp_updater exists
  // and Developer Mode can boot from unsigned kernel (it's a bit stronger check
  // than developer mode only).
  if (!system.OnlyBootSignedKernel() &&
      base::PathExists(base::FilePath(kUpdateDisableFile))) {
    return true;
  }

  return false;
}

std::optional<brillo::SafeFD> OpenDirectorySafely(
    const base::FilePath& dir_path) {
  auto [root_fd, root_err] = brillo::SafeFD::Root();
  if (brillo::SafeFD::IsError(root_err)) {
    return std::nullopt;
  }

  auto [dir_fd, dir_err] = root_fd.OpenExistingDir(dir_path);
  if (brillo::SafeFD::IsError(dir_err)) {
    return std::nullopt;
  }

  return std::move(dir_fd);
}

std::optional<brillo::SafeFD> OpenFileForWritingSafely(
    brillo::SafeFD& dir_fd, const std::string& file_name, mode_t permissions) {
  auto [file_fd, file_err] =
      brillo::OpenOrRemakeFile(&dir_fd, file_name, permissions, geteuid(),
                               getegid(), O_WRONLY | O_APPEND | O_CLOEXEC);
  if (brillo::SafeFD::IsError(file_err)) {
    return std::nullopt;
  }

  // SafeFD::MakeFile() only checks uid, gid, and 0777 mode bits on an existing
  // path, not the file type, so reject non-regular files such as FIFOs (with an
  // active reader) or device nodes.
  struct stat file_stat;
  if (fstat(file_fd.get(), &file_stat) != 0 || !S_ISREG(file_stat.st_mode)) {
    return std::nullopt;
  }
  if ((file_stat.st_mode & ALLPERMS) != permissions &&
      HANDLE_EINTR(fchmod(file_fd.get(), permissions)) != 0) {
    PLOG(ERROR) << "Failed to set permissions on file";
    return std::nullopt;
  }

  return std::move(file_fd);
}

base::ScopedFILE SafeFDToScopedFILE(brillo::SafeFD fd, const char* mode) {
  // Duplicate the underlying descriptor with close-on-exec and wrap it in a
  // FILE* stream via fdopen() so the returned ScopedFILE owns an independent
  // descriptor from |fd|.
  base::ScopedFD dup_fd(HANDLE_EINTR(fcntl(fd.get(), F_DUPFD_CLOEXEC, 0)));
  if (!dup_fd.is_valid()) {
    PLOG(ERROR) << "Failed to duplicate file descriptor";
    return nullptr;
  }

  base::ScopedFILE stream(fdopen(dup_fd.get(), mode));
  if (!stream) {
    PLOG(ERROR) << "Failed to open file stream";
    return nullptr;
  }
  std::ignore = dup_fd.release();
  return stream;
}

// brillo::UpdateLogSymlinks() is unsafe for root in a biod-writable directory:
// it operates on raw paths and calls base::Move(), which follows symlinks via
// stat() and can fall back to recursive base::CopyDirectory() without
// O_NOFOLLOW (allowing arbitrary root file disclosure and overwrite).
//
// Instead, operate relative to |dir_fd| without following symlinks: remove
// obstacles via brillo::DeletePath(), rotate |latest_symlink| with renameat()
// only if it is a symlink owned by |expected_uid|, and create the new link with
// symlinkat().
void UpdateLogSymlinksSafely(brillo::SafeFD& dir_fd,
                             const std::string& latest_symlink,
                             const std::string& previous_symlink,
                             const std::string& log_file_name,
                             uid_t expected_uid) {
  // renameat() and symlinkat() accept paths with directory components, so
  // make sure all operations stay inside |dir_fd|.
  for (const std::string& name :
       {latest_symlink, previous_symlink, log_file_name}) {
    if (name.empty() || name == "." || name == ".." ||
        name.find('/') != std::string::npos) {
      LOG(ERROR) << "Invalid log symlink file name '" << name << "'";
      return;
    }
  }

  if (!brillo::DeletePath(&dir_fd, previous_symlink, /*deep=*/true)) {
    PLOG(ERROR) << "Failed to remove " << previous_symlink;
  }
  struct stat latest_stat;
  if (fstatat(dir_fd.get(), latest_symlink.c_str(), &latest_stat,
              AT_SYMLINK_NOFOLLOW) == 0) {
    if (!S_ISLNK(latest_stat.st_mode) || latest_stat.st_uid != expected_uid ||
        HANDLE_EINTR(renameat(dir_fd.get(), latest_symlink.c_str(),
                              dir_fd.get(), previous_symlink.c_str())) != 0) {
      brillo::DeletePath(&dir_fd, latest_symlink, /*deep=*/true);
    }
  }
  if (HANDLE_EINTR(symlinkat(log_file_name.c_str(), dir_fd.get(),
                             latest_symlink.c_str())) != 0) {
    PLOG(ERROR) << "Unable to create symbolic link from " << latest_symlink
                << " to " << log_file_name;
  }
}

bool SetupLogging(const base::FilePath& log_dir_path) {
  if (LogSetupDirectory(log_dir_path)) {
    return true;
  }

  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty |
                  brillo::kLogHeader);
  LOG(ERROR) << "Failed to set up log directory '" << log_dir_path.value()
             << "', using syslog and stderr logging.";
  return false;
}

}  // namespace updater
}  // namespace biod
