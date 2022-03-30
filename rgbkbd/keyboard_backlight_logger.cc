// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rgbkbd/keyboard_backlight_logger.h"

#include <stdint.h>
#include <string>

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/strings/strcat.h"

namespace rgbkbd {

namespace {

const char kTempLogFilePath[] = "/tmp/rgbkbd_log";

}  // namespace

KeyboardBacklightLogger::KeyboardBacklightLogger() {
  if (!InitializeFile()) {
    LOG(ERROR) << "Failed to initially create or open log file.";
  }
}

bool KeyboardBacklightLogger::SetKeyColor(uint32_t key,
                                          uint8_t r,
                                          uint8_t g,
                                          uint8_t b) {
  const std::string log = base::StrCat(
      {"RGB::SetKeyColor - ", std::to_string(key), ",", std::to_string(r), ",",
       std::to_string(g), ",", std::to_string(b)});
  return WriteLogEntry(log);
}

bool KeyboardBacklightLogger::SetAllKeyColors(uint8_t r, uint8_t g, uint8_t b) {
  const std::string log =
      base::StrCat({"RGB::SetAllKeyColors - ", std::to_string(r), ",",
                    std::to_string(g), ",", std::to_string(b)});
  return WriteLogEntry(log);
}

bool KeyboardBacklightLogger::InitializeFile() {
  const base::FilePath path(kTempLogFilePath);

  // If the file exists, delete it to start fresh.
  base::DeleteFile(path);

  // Create the file.
  file_ = std::make_unique<base::File>(
      path,
      base::File::Flags::FLAG_WRITE | base::File::Flags::FLAG_CREATE_ALWAYS);

  if (!file_->IsValid()) {
    file_.reset();
    return false;
  }

  if (file_->Seek(base::File::Whence::FROM_END, 0) < 0) {
    LOG(ERROR) << "Failed to seek to end of log file.";
    file_.reset();
    return false;
  }

  return true;
}

bool KeyboardBacklightLogger::WriteLogEntry(const std::string& log) {
  if (!file_) {
    LOG(ERROR) << "Attempted to write log to a non-existant file.";
    return false;
  }

  // Add a new line between each log.
  const std::string to_write = base::StrCat({log, "\n"});

  const int len = to_write.length();
  return (len == file_->WriteAtCurrentPos(to_write.data(), len));
}

}  // namespace rgbkbd
