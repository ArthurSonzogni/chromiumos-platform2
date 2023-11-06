// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secanomalyd/mounts.h"

#include <optional>
#include <string>
#include <string_view>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_split.h>

namespace secanomalyd {

namespace {
constexpr char kProcSelfMountsPath[] = "/proc/self/mounts";
}

MaybeMountEntries ReadMounts() {
  std::string proc_mounts;
  if (!base::ReadFileToStringNonBlocking(base::FilePath(kProcSelfMountsPath),
                                         &proc_mounts)) {
    PLOG(ERROR) << "Failed to read " << kProcSelfMountsPath;
    return std::nullopt;
  }

  return ReadMountsFromString(proc_mounts);
}

MaybeMountEntries ReadMountsFromString(const std::string& mounts) {
  std::vector<std::string_view> pieces = base::SplitStringPiece(
      mounts, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  if (pieces.empty()) {
    return std::nullopt;
  }

  MountEntries res;
  for (const auto& piece : pieces) {
    MountEntry e = MountEntry(piece);
    res.push_back(e);
  }

  return MaybeMountEntries(res);
}

MaybeMountEntries FilterPrivateMounts(const MaybeMountEntries& all_mounts) {
  MountEntries uploadable_mounts;
  if (all_mounts) {
    std::copy_if(all_mounts->begin(), all_mounts->end(),
                 std::back_inserter(uploadable_mounts),
                 [](const MountEntry& e) { return !e.IsUsbDriveOrArchive(); });
  }
  return MaybeMountEntries(uploadable_mounts);
}

}  // namespace secanomalyd
