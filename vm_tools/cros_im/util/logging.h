// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CROS_IM_UTIL_LOGGING_H_
#define VM_TOOLS_CROS_IM_UTIL_LOGGING_H_

#include <sstream>

// Support for LOG(INFO), LOG(WARNING), LOG(ERROR), similar to base/logging.h

namespace logging {

enum class LogSeverity {
  INFO,
  WARNING,
  ERROR,
};

#define LOG(severity)                                                         \
  ::logging::LogMessage(__FILE__, __LINE__, ::logging::LogSeverity::severity) \
      .stream()

class LogMessage {
 public:
  LogMessage(const char* file, int line, LogSeverity severity);
  LogMessage(const LogMessage&) = delete;
  LogMessage& operator=(const LogMessage&) = delete;
  ~LogMessage();

  std::ostream& stream() { return stream_; }

 private:
  std::ostringstream stream_;
};

}  // namespace logging

#endif  // VM_TOOLS_CROS_IM_UTIL_LOGGING_H_
