// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef DLP_FILE_ID_H_
#define DLP_FILE_ID_H_

#include <string>
#include <utility>

namespace dlp {

// Files are identified in the daemon by a pair of inode number and crtime
// (creation time).
typedef std::pair<ino64_t, time_t> FileId;

FileId GetFileId(const std::string& path);

}  // namespace dlp

#endif  // DLP_FILE_ID_H_
