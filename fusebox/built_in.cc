// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fusebox/built_in.h"

#include <utility>
#include <vector>

#include <base/logging.h>

#include "fusebox/make_stat.h"

namespace fusebox {
namespace {
// kFuseStatusContentsLen equals strlen(kFuseStatusContentsPtr).
constexpr size_t kFuseStatusContentsLen = 3;
constexpr char kFuseStatusContentsPtr[] = "ok\n";
constexpr char kFuseStatusFilename[] = "fuse_status";
}  // namespace

void BuiltInEnsureNodes(InodeTable& itab) {
  itab.Ensure(INO_BUILT_IN, kFuseStatusFilename, 0, INO_BUILT_IN_FUSE_STATUS);
}

void BuiltInGetStat(ino_t ino, struct stat* stat) {
  *stat = {0};
  switch (ino) {
    case INO_BUILT_IN_FUSE_STATUS:
      stat->st_dev = 1;
      stat->st_ino = ino;
      stat->st_mode = S_IFREG | 0444;
      stat->st_nlink = 1;
      stat->st_uid = kChronosUID;
      stat->st_gid = kChronosAccessGID;
      stat->st_size = kFuseStatusContentsLen;
      return;
  }
}

void BuiltInLookup(std::unique_ptr<EntryRequest> request,
                   const std::string& name) {
  if (name == kFuseStatusFilename) {
    fuse_entry_param entry = {0};
    entry.ino = INO_BUILT_IN_FUSE_STATUS;
    BuiltInGetStat(entry.ino, &entry.attr);
    entry.attr_timeout = kStatTimeoutSeconds;
    entry.entry_timeout = kEntryTimeoutSeconds;
    request->ReplyEntry(entry);
    return;
  }

  errno = request->ReplyError(ENOENT);
  PLOG(ERROR) << "BuiltInLookup";
}

void BuiltInRead(std::unique_ptr<BufferRequest> request,
                 ino_t ino,
                 size_t size,
                 off_t off) {
  switch (ino) {
    case INO_BUILT_IN_FUSE_STATUS: {
      if ((off < 0) || (kFuseStatusContentsLen <= off)) {
        size = 0;
        off = 0;
      } else if (size > (kFuseStatusContentsLen - off)) {
        size = kFuseStatusContentsLen - off;
      }
      request->ReplyBuffer(kFuseStatusContentsPtr + off, size);
      return;
    }
  }

  errno = request->ReplyError(ENOENT);
  PLOG(ERROR) << "BuiltInRead";
}

void BuiltInReadDir(off_t off, DirEntryResponse* response) {
  std::vector<DirEntry> entries;

  if (off == 0) {
    off = 1;
    entries.push_back(
        {INO_BUILT_IN_FUSE_STATUS, kFuseStatusFilename, S_IFREG | 0444});
  }

  response->Append(std::move(entries), true);
}

}  // namespace fusebox
