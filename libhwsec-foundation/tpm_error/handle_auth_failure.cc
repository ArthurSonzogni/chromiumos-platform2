// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec-foundation/tpm_error/handle_auth_failure.h"

#include <stddef.h>
#include <sys/wait.h>

#include <algorithm>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/strings/string_split.h>
#include <re2/re2.h>

#include "libhwsec-foundation/da_reset/da_resetter.h"
#include "libhwsec-foundation/tpm_error/auth_failure_analysis.h"
#include "libhwsec-foundation/tpm_error/tpm_error_data.h"
#include "libhwsec-foundation/tpm_error/tpm_error_uma_reporter.h"

namespace {

constexpr int64_t kLogMaxSize = 20'000;
constexpr int64_t kLogRemainingSize = 10'000;
constexpr std::hash<std::u32string> data_hasher;
char lastError[256] = {'\0'};

base::FilePath logFile;
base::FilePath permanentLogFile;

// Set the error in order to let consumer, e.g tcsd, fetch the error by
// FetchAuthFailureError().
void SetLastError(const std::string& msg) {
  std::string error_msg = msg + ": " + strerror(errno);
  strncpy(lastError, error_msg.c_str(), sizeof(lastError) - 1);
}

// Append |msg| to |log_path|, and limit the size of log to |kLogMaxSize|;
bool AppendMessage(const base::FilePath& log_path, const std::string& msg) {
  if (!base::PathExists(log_path)) {
    return base::WriteFile(log_path, msg);
  }
  if (!base::AppendToFile(log_path, msg)) {
    return false;
  }

  int64_t file_size;
  if (!base::GetFileSize(log_path, &file_size)) {
    return false;
  }
  if (file_size >= kLogMaxSize) {
    std::string contents;
    if (!base::ReadFileToString(log_path, &contents)) {
      return false;
    }
    // Truncate log size to |kLogRemainingSize|.
    int64_t truncate_size = (int64_t)contents.size() - kLogRemainingSize;
    contents.erase(0, truncate_size);
    return base::WriteFile(log_path, contents);
  }
  return true;
}

// Handle any log message in this file, and send them to |logFile| and
// |permanentLogFile| which is set by InitializeAuthFailureLogging().
bool LogMessageHandler(int severity,
                       const char* file,
                       int line,
                       size_t message_start,
                       const std::string& str) {
  // Skip if the message is not genenrated by this file.
  if (strncmp(file, __FILE__, sizeof(__FILE__)) != 0) {
    return false;
  }
  if (!AppendMessage(logFile, str) || !AppendMessage(permanentLogFile, str)) {
    SetLastError("error logging");
  }
  return severity != logging::LOGGING_FATAL;
}

// This will log command to the file set by InitializeAuthFailureLogging().
void LogAuthFailureCommand(const struct TpmErrorData& data) {
  LOG(WARNING) << "auth failure: command " << data.command << ", response "
               << data.response;
}

constexpr LazyRE2 auth_failure_command = {
    R"(auth failure: command (\d+), response (\d+))"};

uint32_t GetCommandHash(const base::FilePath& log_path) {
  std::string contents;
  if (!base::ReadFileToString(log_path, &contents)) {
    return 0;
  }
  auto lines = base::SplitString(contents, "\n", base::KEEP_WHITESPACE,
                                 base::SPLIT_WANT_NONEMPTY);

  // Parse TpmErrorData from auth failure log.
  std::vector<struct TpmErrorData> data_collect;
  for (const std::string& line : lines) {
    struct TpmErrorData data;
    if (!RE2::PartialMatch(line, *auth_failure_command, &data.command,
                           &data.response)) {
      continue;
    }
    data_collect.push_back(data);
  }

  // Uniquify collcection of TpmErrorData.
  std::sort(data_collect.begin(), data_collect.end());
  auto it = std::unique(data_collect.begin(), data_collect.end());
  data_collect.resize(std::distance(data_collect.begin(), it));

  // Convert collection to u32string, so that we can use std::hash.
  std::u32string data_string;
  for (auto& data : data_collect) {
    data_string.push_back(data.command);
    data_string.push_back(data.response);
  }
  return data_hasher(data_string);
}

}  // namespace

extern "C" int FetchAuthFailureError(char out[], size_t size) {
  if (size <= 1)
    return 0;
  if (lastError[0] == '\0')
    return 0;
  strncpy(out, lastError, size - 1);
  out[size - 1] = '\0';
  lastError[0] = '\0';
  return 1;
}

extern "C" void InitializeAuthFailureLogging(const char* log_path,
                                             const char* permanent_log_path) {
  CHECK(logging::GetLogMessageHandler() == nullptr)
      << "LogMessageHandler has already been set";
  logFile = base::FilePath(log_path);
  permanentLogFile = base::FilePath(permanent_log_path);
  logging::SetLogMessageHandler(LogMessageHandler);
}

extern "C" int CheckAuthFailureHistory(const char* current_path,
                                       const char* previous_path,
                                       size_t* auth_failure_hash) {
  base::FilePath current_log(current_path);
  base::FilePath previous_log(previous_path);

  if (!base::PathExists(current_log)) {
    return 0;
  }

  int64_t size;
  if (!base::GetFileSize(current_log, &size)) {
    SetLastError("error checking file size");
    return 0;
  }
  // If there is no failure log in |current_log|, nothing to do here.
  if (size == 0) {
    return 0;
  }

  if (!base::Move(current_log, previous_log)) {
    SetLastError("error moving file");
    return 0;
  }
  if (auth_failure_hash) {
    *auth_failure_hash = GetCommandHash(previous_log);
  }
  return 1;
}

extern "C" int HandleAuthFailure(const struct TpmErrorData* data) {
  if (!hwsec_foundation::DoesCauseDAIncrease(*data)) {
    return true;
  }

  LogAuthFailureCommand(*data);

  hwsec_foundation::TpmErrorUmaReporter reporter;

  reporter.Report(*data);

  hwsec_foundation::DAResetter resetter;
  return resetter.ResetDictionaryAttackLock();
}
