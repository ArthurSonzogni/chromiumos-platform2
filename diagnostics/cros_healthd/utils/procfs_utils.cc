// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/procfs_utils.h"

#include <cstddef>
#include <string_view>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/pattern.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

namespace diagnostics {

const char kProcessCmdlineFile[] = "cmdline";
const char kProcessStatFile[] = "stat";
const char kProcessStatmFile[] = "statm";
const char kProcessStatusFile[] = "status";
const char kProcessIOFile[] = "io";

namespace {

// Returns true if `c` is a hexadecimal number or a dash.
bool IsXdigitOrDash(char c) {
  return ::isxdigit(c) || c == '-';
}

}  // namespace

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

std::optional<int> GetArcVmPid(const base::FilePath& root_dir) {
  base::FilePath proc_dir = root_dir.Append("proc");
  base::FileEnumerator enumerator(proc_dir, /*recursive=*/false,
                                  base::FileEnumerator::DIRECTORIES, "*");
  for (auto file = enumerator.Next(); !file.empty(); file = enumerator.Next()) {
    const std::string basename = file.BaseName().value();
    // Check if the base name only consists of numeric characters.
    if (!std::all_of(basename.begin(), basename.end(), ::isdigit)) {
      continue;
    }

    int pid = 0;
    if (!base::StringToInt(basename, &pid)) {
      LOG(ERROR) << "Failed to parse basename: " << basename;
      break;
    }

    const base::FilePath cmdline = file.Append("cmdline");
    std::string content;
    if (!base::ReadFileToString(cmdline, &content)) {
      // It's possible for a process to disappear between enumeration and
      // reading.
      continue;
    }
    if (base::MatchPattern(content, "/usr/bin/crosvm*--syslog-tag*ARCVM*")) {
      return pid;
    }
  }
  return std::nullopt;
}

std::optional<uint64_t> ParseIomemContent(std::string_view content) {
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
    if (label != "System RAM") {
      continue;
    }
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
  if (total_bytes == 0) {
    return std::nullopt;
  }

  return total_bytes;
}

std::optional<ProcSmaps> ParseProcSmaps(std::string_view content) {
  ProcSmaps smaps;
  std::vector<std::string_view> lines = base::SplitStringPiece(
      content, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  bool inside_guest_memory = false;
  for (const auto& line : lines) {
    std::vector<std::string_view> pieces = base::SplitStringPiece(
        line, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    if (pieces.size() < 2) {
      continue;
    }
    // Check if this line is the beginning of a new memory region. Examples:
    // 575f6f771000-575f7038e000 r-xp 00000000 b3:05 36340 /usr/bin/crosvm
    // 7980712e6000-79813e7e6000 rw-s 00100000 00:01 164   /memfd:crosvm_guest
    if (std::all_of(pieces[0].begin(), pieces[0].end(), IsXdigitOrDash)) {
      inside_guest_memory = base::MatchPattern(line, "*/memfd:crosvm_guest*");
    }
    if (inside_guest_memory) {
      if (pieces[0] == "Rss:") {
        int64_t rss = 0;
        if (!base::StringToInt64(pieces[1], &rss)) {
          LOG(ERROR) << "Incorrectly formatted Rss: " << pieces[1];
          return std::nullopt;
        }
        // Multiple by 1024 as numbers in `smaps` file are in kib.
        smaps.crosvm_guest_rss += rss * 1024;
      } else if (pieces[0] == "Swap:") {
        int64_t swap = 0;
        if (!base::StringToInt64(pieces[1], &swap)) {
          LOG(ERROR) << "Incorrectly formatted Swap: " << pieces[1];
          return std::nullopt;
        }
        smaps.crosvm_guest_swap += swap * 1024;
      }
    }
  }
  // No information is collected.
  if (smaps == ProcSmaps()) {
    return std::nullopt;
  }

  return smaps;
}

}  // namespace diagnostics
