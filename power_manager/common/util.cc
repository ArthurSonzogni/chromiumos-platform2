// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/common/util.h"

#include <glib.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>

#include "base/basictypes.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "power_manager/common/power_constants.h"

namespace {

const char kWakeupCountPath[] = "/sys/power/wakeup_count";

// Path to program used to run code as root.
const char kSetuidHelperPath[] = "/usr/bin/powerd_setuid_helper";

}  // namespace

namespace power_manager {
namespace util {

bool OOBECompleted() {
  return access("/home/chronos/.oobe_completed", F_OK) == 0;
}

void Launch(const char* command) {
  LOG(INFO) << "Launching \"" << command << "\"";
  pid_t pid = fork();
  if (pid == 0) {
    // Detach from parent so that powerd doesn't need to wait around for us
    setsid();
    exit(fork() == 0 ? system(command) : 0);
  } else if (pid > 0) {
    waitpid(pid, NULL, 0);
  }
}

void Run(const char* command) {
  LOG(INFO) << "Running \"" << command << "\"";
  int return_value = system(command);
  if (return_value)
    LOG(ERROR) << "Command failed with " << return_value;
}

void RunSetuidHelper(const std::string& action,
                     const std::string& additional_args,
                     bool wait_for_completion) {
  std::string command = kSetuidHelperPath + std::string(" --action=" + action);
  if (!additional_args.empty())
    command += " " + additional_args;
  if (wait_for_completion)
    Run(command.c_str());
  else
    Launch(command.c_str());
}

void CreateStatusFile(const FilePath& file) {
  if (!file_util::WriteFile(file, NULL, 0) == -1)
    LOG(ERROR) << "Unable to create " << file.value();
  else
    LOG(INFO) << "Created " << file.value();
}

void RemoveStatusFile(const FilePath& file) {
  if (file_util::PathExists(file)) {
    if (!file_util::Delete(file, false))
      LOG(ERROR) << "Unable to remove " << file.value();
    else
      LOG(INFO) << "Removed " << file.value();
  }
}

bool GetWakeupCount(unsigned int* value) {
  int64 temp_value;
  FilePath path(kWakeupCountPath);
  std::string buf;
  if (file_util::ReadFileToString(path, &buf)) {
    TrimWhitespaceASCII(buf, TRIM_TRAILING, &buf);
    if (base::StringToInt64(buf, &temp_value)) {
      *value = static_cast<unsigned int>(temp_value);
      return true;
    } else {
      LOG(ERROR) << "Garbage found in " << path.value();
    }
  }
  LOG(INFO) << "Could not read " << path.value();
  return false;
}

bool GetUintFromFile(const char* filename, unsigned int* value) {
  std::string buf;

  FilePath path(filename);
  if (!file_util::ReadFileToString(path, &buf)) {
    LOG(ERROR) << "Unable to read " << filename;
    return false;
  }
  TrimWhitespaceASCII(buf, TRIM_TRAILING, &buf);
  if (base::StringToUint(buf, value))
      return true;
  LOG(ERROR) << "Garbage found in " << filename << "( " << buf << " )";
  return false;
}

const char* InputTypeToString(InputType type) {
  switch (type) {
    case INPUT_LID:
      return "input(LID)";
    case INPUT_POWER_BUTTON:
      return "input(POWER_BUTTON)";
    case INPUT_UNHANDLED:
      return "input(UNHANDLED)";
    default:
      NOTREACHED();
      return "";
  }
}

void RemoveTimeout(guint* timeout_id) {
  if (*timeout_id) {
    g_source_remove(*timeout_id);
    *timeout_id = 0;
  }
}

}  // namespace util
}  // namespace power_manager
