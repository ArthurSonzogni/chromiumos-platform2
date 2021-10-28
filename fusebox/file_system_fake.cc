// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fusebox/file_system_fake.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
#include <base/logging.h>
#include <base/no_destructor.h>
#include <base/strings/string_piece.h>

#include "fusebox/fuse_file_handles.h"
#include "fusebox/fuse_path_inodes.h"
#include "fusebox/fuse_request.h"
#include "fusebox/make_stat.h"
#include "fusebox/util.h"

namespace {

void ShowStat(const struct stat& stat, const std::string& name = {}) {
  const char* type = S_ISDIR(stat.st_mode) ? "DIR" : "REG";

  LOG(INFO) << " ENTRY: " << name;
  LOG(INFO) << "    ls: " << fusebox::StatModeToString(stat.st_mode);
  LOG(INFO) << "  mode: " << type << std::hex << " " << stat.st_mode;
  LOG(INFO) << "   dev: " << stat.st_dev;
  LOG(INFO) << "   ino: " << stat.st_ino;
  LOG(INFO) << "  size: " << stat.st_size;
  LOG(INFO) << " nlink: " << stat.st_nlink;
  LOG(INFO) << "  rdev: " << stat.st_rdev;
  LOG(INFO) << "   uid: " << stat.st_uid;
  LOG(INFO) << "   gid: " << stat.st_gid;
  LOG(INFO) << " atime: " << stat.st_atime;
  LOG(INFO) << " mtime: " << stat.st_mtime;
  LOG(INFO) << " ctime: " << stat.st_ctime;
}

}  // namespace

namespace fusebox {

static auto& GetInodeTable() {
  static base::NoDestructor<InodeTable> inode_table;
  return *inode_table;
}

static auto& GetDirEntryVector() {
  static base::NoDestructor<std::vector<struct DirEntry>> entry_vector;
  return *entry_vector;
}

FileSystemFake::FileSystemFake() = default;

FileSystemFake::~FileSystemFake() = default;

void FileSystemFake::Init(void* userdata, struct fuse_conn_info*) {
  LOG(INFO) << "Init";
  CHECK(userdata);

  const auto time_now = std::time(nullptr);
  const bool read_only = true;
  struct stat parent;  // parent node: assume `pwd`.
  CHECK_EQ(0, stat(".", &parent));

  struct stat stat = {0};
  stat.st_atime = time_now;
  stat.st_mtime = time_now;
  stat.st_ctime = time_now;

  Node* root = GetInodeTable().Lookup(1);
  stat.st_mode = S_IFDIR | 0777;
  auto root_stat = MakeStat(root->ino, stat, read_only);
  GetInodeTable().SetStat(root->ino, root_stat);
  ShowStat(root_stat, root->name);

  Node* hello = GetInodeTable().Create(1, "hello");
  stat.st_mode = S_IFREG | 0777;
  stat.st_size = strlen("hello\r\n");
  auto hello_stat = MakeStat(hello->ino, stat, read_only);
  GetInodeTable().SetStat(hello->ino, hello_stat);
  ShowStat(hello_stat, hello->name);

  std::vector<struct DirEntry>& entry = GetDirEntryVector();
  entry.push_back({root->ino, ".", root_stat.st_mode});
  entry.push_back({parent.st_ino, "..", parent.st_mode});
  entry.push_back({hello->ino, "hello", hello_stat.st_mode});
}

void FileSystemFake::Lookup(std::unique_ptr<EntryRequest> request,
                            fuse_ino_t parent,
                            const char* name) {
  LOG(INFO) << "Lookup parent " << parent << " name " << name;

  if (request->IsInterrupted())
    return;

  Node* node = GetInodeTable().Lookup(parent, name);
  if (!node) {
    PLOG(ERROR) << " lookup error";
    request->ReplyError(errno);
    return;
  }

  struct stat stat;
  CHECK(GetInodeTable().GetStat(node->ino, &stat));
  CHECK_EQ(stat.st_ino, node->ino);

  const double kEntryTimeoutSeconds = 5.0;
  fuse_entry_param entry = {0};
  entry.ino = node->ino;
  entry.attr = stat;
  entry.attr_timeout = kEntryTimeoutSeconds;
  entry.entry_timeout = kEntryTimeoutSeconds;

  LOG(INFO) << " found ino " << node->ino;
  request->ReplyEntry(entry);
}

void FileSystemFake::GetAttr(std::unique_ptr<AttrRequest> request,
                             fuse_ino_t ino) {
  LOG(INFO) << "GetAttr ino " << ino;

  if (request->IsInterrupted())
    return;

  Node* node = GetInodeTable().Lookup(ino);
  if (!node) {
    PLOG(ERROR) << " getattr error";
    request->ReplyError(errno);
    return;
  }

  struct stat stat;
  CHECK(GetInodeTable().GetStat(node->ino, &stat));
  CHECK_EQ(stat.st_ino, node->ino);

  const double kStatTimeoutSeconds = 5.0;
  request->ReplyAttr(stat, kStatTimeoutSeconds);
}

void FileSystemFake::OpenDir(std::unique_ptr<OpenRequest> request,
                             fuse_ino_t ino) {
  LOG(INFO) << "OpenDir ino " << ino;

  if (request->IsInterrupted())
    return;

  Node* node = GetInodeTable().Lookup(ino);
  if (!node) {
    PLOG(ERROR) << " opendir error";
    request->ReplyError(errno);
    return;
  }

  struct stat stat;
  CHECK(GetInodeTable().GetStat(node->ino, &stat));
  CHECK_EQ(stat.st_ino, node->ino);

  if (!S_ISDIR(stat.st_mode)) {
    LOG(ERROR) << " opendir error: ENOTDIR";
    request->ReplyError(ENOTDIR);
    return;
  }

  LOG(INFO) << " " << OpenFlagsToString(request->flags());
  if ((request->flags() & O_ACCMODE) != O_RDONLY) {
    LOG(ERROR) << " opendir error: EACCES";
    request->ReplyError(EACCES);
    return;
  }

  uint64_t handle = fusebox::OpenFile();
  readdir_[handle].reset(new DirEntryResponse(handle));

  LOG(INFO) << " opendir fh " << handle;
  request->ReplyOpen(handle);
}

void FileSystemFake::ReadDir(std::unique_ptr<DirEntryRequest> request,
                             fuse_ino_t ino,
                             off_t off) {
  LOG(INFO) << "ReadDir ino " << ino << " off " << off;

  if (request->IsInterrupted())
    return;

  auto it = readdir_.find(request->fh());
  if (it == readdir_.end()) {
    LOG(ERROR) << " readdir error: EBADF " << request->fh();
    request->ReplyError(EBADF);
    return;
  }

  DirEntryResponse* response = it->second.get();
  if (off == 0) {
    LOG(INFO) << " readdir fh " << request->fh();
    for (const auto& entry : GetDirEntryVector())
      LOG(INFO) << " entry [" << entry.name << "]";
    response->Append(GetDirEntryVector(), true);
  }

  response->Append(std::move(request));
}

void FileSystemFake::ReleaseDir(std::unique_ptr<OkRequest> request,
                                fuse_ino_t ino) {
  LOG(INFO) << "ReleaseDir ino " << ino;

  if (request->IsInterrupted())
    return;

  if (!fusebox::GetFile(request->fh())) {
    LOG(ERROR) << " releasedir error: EBADF " << request->fh();
    request->ReplyError(EBADF);
    return;
  }

  LOG(INFO) << " releasedir fh " << request->fh();
  fusebox::CloseFile(request->fh());
  readdir_.erase(request->fh());

  request->ReplyOk();
}

void FileSystemFake::Open(std::unique_ptr<OpenRequest> request,
                          fuse_ino_t ino) {
  LOG(INFO) << "Open ino " << ino;

  if (request->IsInterrupted())
    return;

  Node* node = GetInodeTable().Lookup(ino);
  if (!node) {
    PLOG(ERROR) << " open error";
    request->ReplyError(errno);
    return;
  }

  struct stat stat;
  CHECK(GetInodeTable().GetStat(node->ino, &stat));
  CHECK_EQ(stat.st_ino, node->ino);

  if (S_ISDIR(stat.st_mode)) {
    LOG(ERROR) << " open error: EISDIR";
    request->ReplyError(EISDIR);
    return;
  }

  LOG(INFO) << " " << OpenFlagsToString(request->flags());
  if ((request->flags() & O_ACCMODE) != O_RDONLY) {
    LOG(ERROR) << " open error: EACCES";
    request->ReplyError(EACCES);
    return;
  }

  uint64_t handle = fusebox::OpenFile();
  LOG(INFO) << " opened fh " << handle;
  request->ReplyOpen(handle);
}

void FileSystemFake::Read(std::unique_ptr<BufferRequest> request,
                          fuse_ino_t ino,
                          size_t size,
                          off_t off) {
  LOG(INFO) << "Read ino " << ino << " off " << off << " size " << size;

  if (request->IsInterrupted())
    return;

  if (!fusebox::GetFile(request->fh())) {
    LOG(ERROR) << " read error: EBADF " << request->fh();
    request->ReplyError(EBADF);
    return;
  }

  struct stat stat;
  CHECK(GetInodeTable().GetStat(ino, &stat));
  CHECK_EQ(stat.st_ino, ino);

  if (S_ISDIR(stat.st_mode)) {
    LOG(ERROR) << " read error: EISDIR";
    request->ReplyError(EISDIR);
    return;
  }

  const std::string data("hello\r\n");
  LOG(INFO) << " read fh " << request->fh();

  const auto get_data_slice = [&data, &off, &size]() {
    auto* source = data.data();
    auto length = data.size();
    if (off < length)
      return base::StringPiece(source + off, std::min(length - off, size));
    return base::StringPiece();
  };

  auto slice = get_data_slice();
  request->ReplyBuffer(slice.data(), slice.size());
}

void FileSystemFake::Release(std::unique_ptr<OkRequest> request,
                             fuse_ino_t ino) {
  LOG(INFO) << "Release ino " << ino;

  if (request->IsInterrupted())
    return;

  if (!fusebox::GetFile(request->fh())) {
    LOG(ERROR) << " release error: EBADF " << request->fh();
    request->ReplyError(EBADF);
    return;
  }

  LOG(INFO) << " release fh " << request->fh();
  fusebox::CloseFile(request->fh());
  request->ReplyOk();
}

}  // namespace fusebox
