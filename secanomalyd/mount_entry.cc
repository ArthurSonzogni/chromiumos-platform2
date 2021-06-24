// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secanomalyd/mount_entry.h"

#include <algorithm>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/strings/string_piece.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

namespace {
// These paths can be sources of removable drive or archive mounts.
const std::vector<base::FilePath> kSrcPathsToFilter = {
    base::FilePath("/home/chronos"),
    base::FilePath("/media/archive"),
    base::FilePath("/media/fuse"),
    base::FilePath("/media/removable"),
    base::FilePath("/run/arc/sdcard/write/emulated/0"),
};

// These paths can be destinations for removable drive or archive mounts.
const std::vector<base::FilePath> kDestPathsToFilter = {
    base::FilePath("/media/archive"),
    base::FilePath("/media/fuse"),
    base::FilePath("/media/removable"),
};

const base::FilePath kUsrLocal = base::FilePath("/usr/local");
}  // namespace

MountEntry::MountEntry(base::StringPiece mount_str) {
  // These entries are of the format:
  // /dev/root / ext2 rw,seclabel,relatime 0 0
  std::vector<base::StringPiece> fields =
      base::SplitStringPiece(mount_str, base::kWhitespaceASCII,
                             base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  src_ = base::FilePath(fields[0].as_string());
  dest_ = base::FilePath(fields[1].as_string());
  type_ = fields[2].as_string();

  opts_ = base::SplitString(fields[3], ",", base::TRIM_WHITESPACE,
                            base::SPLIT_WANT_NONEMPTY);
}

bool MountEntry::IsWX() {
  return std::find(opts_.begin(), opts_.end(), "rw") != opts_.end() &&
         std::find(opts_.begin(), opts_.end(), "noexec") == opts_.end();
}

bool MountEntry::IsUsbDriveOrArchive() {
  for (const auto& src_path_to_filter : kSrcPathsToFilter) {
    if (src_path_to_filter.IsParent(src_)) {
      return true;
    }
  }

  for (const auto& dest_path_to_filter : kDestPathsToFilter) {
    if (dest_path_to_filter.IsParent(dest_)) {
      return true;
    }
  }

  return false;
}

bool MountEntry::IsDestInUsrLocal() {
  return kUsrLocal == this->dest() || kUsrLocal.IsParent(this->dest());
}
