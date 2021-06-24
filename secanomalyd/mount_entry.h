// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// MountEntry objects represent entries in the list of mounts obtained from
// /proc/<pid>/mounts.

#ifndef SECANOMALYD_MOUNT_ENTRY_H_
#define SECANOMALYD_MOUNT_ENTRY_H_

#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/strings/string_piece.h>

class MountEntry {
 public:
  MountEntry() : src_{}, dest_{}, type_{}, opts_{} {}

  explicit MountEntry(base::StringPiece mount_str);

  // Copying the private fields is fine.
  MountEntry(const MountEntry& other) = default;
  MountEntry& operator=(const MountEntry& other) = default;

  // TODO(jorgelo): Implement move constructor so that we can use emplace().

  bool IsWX();
  bool IsUsbDriveOrArchive();
  bool IsDestInUsrLocal();

  const base::FilePath& src() const { return src_; }
  const base::FilePath& dest() const { return dest_; }
  const std::string& type() const { return type_; }

 private:
  base::FilePath src_;
  base::FilePath dest_;
  std::string type_;
  std::vector<std::string> opts_;
};

#endif  // SECANOMALYD_MOUNT_ENTRY_H_
