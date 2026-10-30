// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/updater/update_utils.h"

#include <optional>
#include <string>
#include <string_view>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/logging/logging_settings.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <brillo/daemons/daemon.h>
#include <brillo/syslog_logging.h>
#include <brillo/vcsid.h>

#include "biod/biod_config.h"

namespace {

constexpr char kUpdateDisableFile[] =
    "/var/lib/bio_fw_updater/.disable_fp_updater";
constexpr char kLatestLogSymlink[] = "bio_fw_updater.LATEST";
constexpr char kPreviousLogSymlink[] = "bio_fw_updater.PREVIOUS";

bool LogSetupDirectory(const base::FilePath& log_dir_path) {
  if (!base::DirectoryExists(log_dir_path)) {
    return false;
  }

  const auto log_file_path = log_dir_path.Append(base::StringPrintf(
      "bio_fw_updater.%s",
      brillo::GetTimeAsLogString(base::Time::Now()).c_str()));

  logging::LoggingSettings logging_settings;
  logging_settings.logging_dest = logging::LOG_TO_FILE;
  logging_settings.log_file_path = log_file_path.value().c_str();
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
  brillo::UpdateLogSymlinks(log_dir_path.Append(kLatestLogSymlink),
                            log_dir_path.Append(kPreviousLogSymlink),
                            log_file_path);
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
