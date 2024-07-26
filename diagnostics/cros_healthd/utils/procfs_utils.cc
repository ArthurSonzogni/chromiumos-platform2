// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/procfs_utils.h"

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

namespace diagnostics {

const char kProcessCmdlineFile[] = "cmdline";
const char kProcessStatFile[] = "stat";
const char kProcessStatmFile[] = "statm";
const char kProcessStatusFile[] = "status";
const char kProcessIOFile[] = "io";

base::FilePath GetProcProcessDirectoryPath(const base::FilePath& root_dir,
                                           pid_t pid) {
  return root_dir.Append("proc").Append(base::NumberToString(pid));
}

base::FilePath GetProcCpuInfoPath(const base::FilePath& root_dir) {
  return root_dir.Append("proc/cpuinfo");
}

base::FilePath GetProcStatPath(const base::FilePath& root_dir) {
  return root_dir.Append("proc/stat");
}

base::FilePath GetProcUptimePath(const base::FilePath& root_dir) {
  return root_dir.Append("proc/uptime");
}

base::FilePath GetProcCryptoPath(const base::FilePath& root_dir) {
  return root_dir.Append("proc/crypto");
}

std::optional<uint64_t> ParseIomemContent(const std::string& content) {
  uint64_t total_bytes = 0;
  // /proc/iomem content looks like this:
  // "00001000-0009ffff : System RAM"
  base::StringPairs pairs;
  if (!base::SplitStringIntoKeyValuePairs(content, ':', '\n', &pairs)) {
    LOG(ERROR) << "Incorrectly formatted /proc/iomem";
    return std::nullopt;
  }

  for (const auto& [raw_range, raw_label] : pairs) {
    // Trim leading/trailing whitespaces.
    std::string_view range =
        base::TrimString(raw_range, " ", base::TrimPositions::TRIM_ALL);
    std::string_view label =
        base::TrimString(raw_label, " ", base::TrimPositions::TRIM_ALL);
    if (label != "System RAM")
      continue;
    auto start_end = base::SplitStringPiece(range, "-", base::TRIM_WHITESPACE,
                                            base::SPLIT_WANT_NONEMPTY);
    if (start_end.size() != 2) {
      LOG(ERROR) << "Incorrectly formatted range: " << range;
      return std::nullopt;
    }
    uint64_t start = 0, end = 0;
    if (!base::HexStringToUInt64(start_end[0], &start) ||
        !base::HexStringToUInt64(start_end[1], &end)) {
      LOG(ERROR) << "Incorrectly formatted range: " << range;
      return std::nullopt;
    }
    uint64_t length = end - start + 1;  // +1 as `end` is inclusive.
    total_bytes += length;
  }

  // |total_bytes| can be 0 if |content| is empty or truncated,
  // which should be treated as an error.
  if (total_bytes == 0)
    return std::nullopt;

  return total_bytes;
}

}  // namespace diagnostics
