// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util/logging.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <unistd.h>

namespace logging {

LogMessage::LogMessage(const char* file, int line, LogSeverity severity) {
  // Message is prefixed as follows:
  // (cros_im:1234) 2022-02-22T12:34:56.789012Z WARNING: [foo.cc(123)]
  stream_ << "(cros_im:" << getpid() << ") ";

  auto now = std::chrono::system_clock::now();
  std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
  auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(
                          now.time_since_epoch()) %
                      1000000;

  stream_ << std::put_time(std::localtime(&now_time_t), "%FT%T.")
          << std::setfill('0') << std::setw(6) << microseconds.count() << "Z ";

  if (severity == LogSeverity::INFO) {
    stream_ << "INFO";
  } else if (severity == LogSeverity::WARNING) {
    stream_ << "WARNING";
  } else if (severity == LogSeverity::ERROR) {
    stream_ << "ERROR";
  } else {
    stream_ << "(unknown severity)";
  }

  std::string file_str = file;
  // We only run on Linux, this is good enough.
  size_t slash = file_str.find_last_of('/');
  if (slash != std::string::npos) {
    file_str = file_str.substr(slash + 1);
  }
  stream_ << ": [" << file_str << "(" << line << ")] ";
}

LogMessage::~LogMessage() {
  std::cerr << stream_.str() << std::endl;
}

}  // namespace logging
