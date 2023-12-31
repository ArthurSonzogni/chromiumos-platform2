//
// Copyright (C) 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>

#include "update_engine/common/logging.h"
#include "update_engine/common/utils.h"

using std::string;

namespace chromeos_update_engine {

namespace {

constexpr char kSystemLogsRoot[] = "/var/log";

void SetupLogSymlink(const string& symlink_path, const string& log_path) {
  // TODO(petkov): To ensure a smooth transition between non-timestamped and
  // timestamped logs, move an existing log to start the first timestamped
  // one. This code can go away once all clients are switched to this version or
  // we stop caring about the old-style logs.
  if (utils::FileExists(symlink_path.c_str()) &&
      !utils::IsSymlink(symlink_path.c_str())) {
    base::ReplaceFile(base::FilePath(symlink_path), base::FilePath(log_path),
                      nullptr);
  }
  base::DeletePathRecursively(base::FilePath(symlink_path));
  if (symlink(log_path.c_str(), symlink_path.c_str()) == -1) {
    PLOG(ERROR) << "Unable to create symlink " << symlink_path
                << " pointing at " << log_path;
  }
}

string SetupLogFile(const string& kLogsRoot) {
  const string kLogSymlink = kLogsRoot + "/update_engine.log";
  const string kLogsDir = kLogsRoot + "/update_engine";
  const string kLogPath =
      base::StringPrintf("%s/update_engine.%s", kLogsDir.c_str(),
                         utils::GetTimeAsString(::time(nullptr)).c_str());
  mkdir(kLogsDir.c_str(), 0755);
  SetupLogSymlink(kLogSymlink, kLogPath);
  return kLogSymlink;
}

}  // namespace

void SetupLogging(bool log_to_system, bool log_to_file) {
  logging::LoggingSettings log_settings;
  log_settings.lock_log = logging::DONT_LOCK_LOG_FILE;
  log_settings.logging_dest = static_cast<logging::LoggingDestination>(
      (log_to_system ? logging::LOG_TO_SYSTEM_DEBUG_LOG : 0) |
      (log_to_file ? logging::LOG_TO_FILE : 0));
  log_settings.log_file = nullptr;

  string log_file;
  if (log_to_file) {
    log_file = SetupLogFile(kSystemLogsRoot);
    log_settings.delete_old = logging::APPEND_TO_OLD_LOG_FILE;
#if BASE_VER < 780000
    log_settings.log_file = log_file.c_str();
#else
    log_settings.log_file_path = log_file.c_str();
#endif
  }
  logging::InitLogging(log_settings);

  // There are libraries that are linked into updage_engine that print into
  // stderr. Doing so causes the printouts to be lost as stderr isn't redirected
  // to update_engine's log file.
  if (log_to_file) {
    FILE* log_file_dupe = logging::DuplicateLogFILE();
    if (log_file_dupe == nullptr) {
      LOG(ERROR) << "Failed to duplicate log file desriptor. "
                    "Skipping stderr redirection";
    } else {
      // Change the file descriptor number for stderr, so writes into
      // fileno(2/stderr) get written into the log file.
      dup2(fileno(log_file_dupe), fileno(stderr));
      // Change the value for those that directly use stderr `FILE*`, so writes
      // into stderr get written into the log file.
      stderr = log_file_dupe;
      // Make it unbuffered so flushing isn't required.
      setbuf(stderr, NULL);
    }
  }
}

}  // namespace chromeos_update_engine
