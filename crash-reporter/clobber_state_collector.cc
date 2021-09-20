// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/clobber_state_collector.h"

#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

namespace {
constexpr size_t kMaxSignature = 256;
constexpr const char kTmpfilesLogPath[] = "/run/tmpfiles.log";
constexpr const char kClobberStateName[] = "clobber-state";

std::string filter_signature(const std::string& sig) {
  static constexpr const char* const known_issues[] = {
      // This is associated with an EXT4-fs error in htree_dirblock_to_tree:
      // "Directory block failed checksum"
      "Bad message",
      // This is associated with an EXT4-fs error in ext4_xattr_block_get:
      // "corrupted xattr block ####"
      "Structure needs cleaning",
  };
  for (auto known_issue : known_issues) {
    if (base::EndsWith(sig, known_issue)) {
      return known_issue;
    }
  }
  return sig;
}

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
    AddCrashMetaData("sig", filter_signature(lines.front()));
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

// static
CollectorInfo ClobberStateCollector::GetHandlerInfo(bool clobber_state) {
  auto clobber_state_collector = std::make_shared<ClobberStateCollector>();
  return {
      .collector = clobber_state_collector,
      .handlers = {{
          .should_handle = clobber_state,
          .cb = base::BindRepeating(&ClobberStateCollector::Collect,
                                    clobber_state_collector),
      }},
  };
}
