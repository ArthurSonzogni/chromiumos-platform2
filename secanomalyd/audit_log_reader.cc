// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secanomalyd/audit_log_reader.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <absl/strings/match.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>

namespace secanomalyd {

// Extracts the executable path from the cmd field of the log message.
static constexpr LazyRE2 kExePathPattern = {R"(cmd=\"(\S+).*\")"};

bool IsMemfdExecutionAttempt(const std::string& log_message,
                             std::string& exe_path) {
  // Looks for the text snippet appended to log messages coming from the kernel
  // LSM code where the execution attempt is blocked.
  if (absl::StartsWith(log_message, "ChromeOS LSM: memfd execution attempt")) {
    if (!RE2::PartialMatch(log_message, *kExePathPattern, &exe_path)) {
      exe_path = secanomalyd::kUnknownExePath;
    }
    return true;
  }
  return false;
}

bool Parser::IsValid(const std::string& line, LogRecord& log_record) {
  double log_time_in_seconds;
  std::string log_message, log_time;
  if (!RE2::FullMatch(line, *pattern_, &log_time, &log_message)) {
    return false;
  }
  if (!base::StringToDouble(log_time, &log_time_in_seconds)) {
    LOG(WARNING) << "Ignoring log entry due to invalid timestamp. time="
                 << log_time << " tag=" << tag_ << " message=" << log_message;
    return false;
  }
  log_record.tag = tag_;
  log_record.message = log_message;
  log_record.timestamp =
      base::Time::FromSecondsSinceUnixEpoch(log_time_in_seconds);
  return true;
}

bool AuditLogReader::GetNextEntry(LogRecord* log_record) {
  std::string line;
  while (log_file_.GetLine(&line)) {
    // If the log record is matched with any of the Parser objects in the
    // |parser_map_| and it is valid, ReadLine returns true and populates the
    // LogRecord object.
    if (ReadLine(line, *log_record)) {
      return true;
    }
  }
  return false;
}

bool AuditLogReader::ReadLine(const std::string& line, LogRecord& log_record) {
  return avc_parser_->IsValid(line, log_record);
}

}  // namespace secanomalyd
