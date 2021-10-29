// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secanomalyd/mounts.h"

#include <string>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_piece.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

namespace secanomalyd {

namespace {
constexpr char kProcSelfMountsPath[] = "/proc/self/mounts";
}

MaybeMountEntries ReadMounts() {
  std::string proc_mounts;
  if (!base::ReadFileToStringNonBlocking(base::FilePath(kProcSelfMountsPath),
                                         &proc_mounts)) {
    PLOG(ERROR) << "Failed to read " << kProcSelfMountsPath;
    return base::nullopt;
  }

  return ReadMountsFromString(proc_mounts);
}

MaybeMountEntries ReadMountsFromString(const std::string& mounts) {
  std::vector<base::StringPiece> pieces = base::SplitStringPiece(
      mounts, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  if (pieces.empty()) {
    return base::nullopt;
  }

  MountEntries res;
  for (const auto& piece : pieces) {
    res.push_back(MountEntry(piece));
  }

  return MaybeMountEntries(res);
}

}  // namespace secanomalyd
