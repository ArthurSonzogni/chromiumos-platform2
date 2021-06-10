// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/clobber_state_collector.h"

#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/strings/string_split.h>

namespace {
constexpr size_t kMaxSignature = 256;
constexpr const char kTmpfilesLogPath[] = "/run/tmpfiles.log";
constexpr const char kClobberStateName[] = "clobber-state";
}  // namespace

ClobberStateCollector::ClobberStateCollector()
    : CrashCollector("clobber_state_collector"),
      tmpfiles_log_(kTmpfilesLogPath) {}

bool ClobberStateCollector::Collect() {
  std::string exec_name(kClobberStateName);
  std::string dump_basename = FormatDumpBasename(exec_name, time(nullptr), 0);

  base::FilePath crash_directory;
  if (!GetCreatedCrashDirectoryByEuid(kRootUid, &crash_directory, nullptr)) {
    return false;
  }

  // Use the first line or first 1024 bytes of the tmpfiles log as the
  // signature with the exec_name as a fall back.
  std::string tmpfiles_log;
  if (!base::ReadFileToStringWithMaxSize(tmpfiles_log_, &tmpfiles_log,
                                         kMaxSignature) &&
      tmpfiles_log.empty()) {
    PLOG(ERROR) << "Failed to read '" << kTmpfilesLogPath << "'";
  }
  auto lines = base::SplitString(tmpfiles_log, "\n", base::TRIM_WHITESPACE,
                                 base::SPLIT_WANT_NONEMPTY);
  if (lines.empty()) {
    // Fall back to the exec name as the crash signature.
    AddCrashMetaData("sig", exec_name);
  } else {
    AddCrashMetaData("sig", lines.front());
  }

  base::FilePath log_path = GetCrashPath(crash_directory, dump_basename, "log");
  base::FilePath meta_path =
      GetCrashPath(crash_directory, dump_basename, "meta");

  bool result = GetLogContents(log_config_path_, exec_name, log_path);
  if (result) {
    FinishCrash(meta_path, exec_name, log_path.BaseName().value());
  }

  return result;
}
