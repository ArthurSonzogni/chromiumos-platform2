// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sysexits.h>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/flag_helper.h>

#include "timberslide/timberslide.h"

using timberslide::TimberSlide;

namespace {
const char kDefaultDeviceLogFile[] = "/sys/kernel/debug/cros_ec/console_log";
const char kDefaultDeviceUptimeFile[] = "/sys/kernel/debug/cros_ec/uptime";
const char kDefaultLogDirectory[] = "/var/log/";
const std::array<std::string_view, 3> kDefaultTokenDatabasePaths = {
    "/usr/share/cros_ec/tokens.bin",
    "/usr/local/usr/share/cros_ec/tokens.bin",
    "/usr/local/cros_ec/tokens.bin",
};
}  // namespace

// Return the first valid path found to token database. If none found
// return empty string.
template <typename It>
std::string_view FindTokenDatabase(It begin, It end) {
  for (; begin != end; begin++) {
    if (base::PathExists(base::FilePath(*begin))) {
      LOG(INFO) << "Found Token DB: " << *begin;
      return *begin;
    }
  }

  return "";
}

int main(int argc, char* argv[]) {
  DEFINE_string(device_log, kDefaultDeviceLogFile,
                "File where the recent EC logs are posted to.");
  DEFINE_string(log_directory, kDefaultLogDirectory,
                "Directory where the output logs should be.");
  DEFINE_string(uptime_file, kDefaultDeviceUptimeFile, "Device uptime file.");
  DEFINE_string(token_db, "", "EC Token database");
  brillo::FlagHelper::Init(
      argc, argv, "timberslide concatenates EC logs for use in debugging.");

  const base::FilePath path(FLAGS_device_log);
  base::File device_file(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!device_file.IsValid()) {
    LOG(ERROR) << "Error opening " << FLAGS_device_log << ": "
               << base::File::ErrorToString(device_file.error_details());
    return EX_UNAVAILABLE;
  }

  const base::FilePath uptime_path(FLAGS_uptime_file);
  base::File uptime_file(uptime_path,
                         base::File::FLAG_OPEN | base::File::FLAG_READ);

  std::string ec_type = path.DirName().BaseName().value();

  if (FLAGS_token_db.empty()) {
    FLAGS_token_db = FindTokenDatabase(kDefaultTokenDatabasePaths.begin(),
                                       kDefaultTokenDatabasePaths.end());
  }

  TimberSlide ts(ec_type, std::move(device_file), std::move(uptime_file),
                 base::FilePath(FLAGS_log_directory),
                 base::FilePath(FLAGS_token_db));

  return ts.Run();
}
