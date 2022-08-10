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
#include <base/numerics/safe_conversions.h>
#include <base/strings/string_piece.h>

#include "fusebox/fuse_file_handles.h"
#include "fusebox/fuse_path_inodes.h"
#include "fusebox/fuse_request.h"
#include "fusebox/make_stat.h"
#include "fusebox/util.h"

namespace fusebox {

static auto& GetInodeTable() {
  static base::NoDestructor<InodeTable> inode_table;
  return *inode_table;
}

class FakeFileEntry {
 public:
  // Creates FakeFileEntry for InodeTable |node| with |mode|.
  static std::pair<ino_t, FakeFileEntry> Create(Node* node, mode_t mode) {
    CHECK(node && node->ino) << __func__ << " invalid node";
    return {node->ino, FakeFileEntry(node, mode)};
  }

  // Returns entry node.
  Node* node() const { return node_; }

  // Returns entry data size: 0 unless the entry is S_ISREG.
  size_t GetSize() const { return data_.size(); }

  // Sets an S_ISREG entry data to |size|. Returns the size.
  size_t SetSize(size_t size) {
    CheckEntryIsReg(__func__);

    data_.resize(size);
    return data_.size();
  }

  // Returns an S_ISREG entry data at |off| of length |size|.
  base::StringPiece GetDataSlice(off_t off, size_t size) const {
    CheckEntryIsReg(__func__);

    size_t length = data_.size();
    if (off < 0 || off >= length)
      return {};  // Out-of-bounds: returns empty slice.

    length -= size_t(off);
    return {data_.data() + off, std::min(length, size)};
  }

  // Sets an S_ISREG entry data at |off| to |buffer| of |size|. Returns
  // number of bytes written (count).
  size_t SetData(const char* buffer, size_t size, off_t off) {
    CheckEntryIsReg(__func__);

    if (!buffer || !size)
      return 0;

    CHECK_GE(off, 0);
    CHECK_EQ(off, size_t(off));

    size_t length = base::saturated_cast<size_t>(size_t(off) + size);
    if (length > data_.size())
      data_.resize(length);

    size_t count = std::min(length - size_t(off), size);
    std::memcpy(data_.data() + size_t(off), buffer, count);
    return count;
  }

 private:
  // Constructs an entry for InodeTable |node| with |mode|.
  FakeFileEntry(Node* node, mode_t mode) : node_(node), mode_(mode) {
    CHECK(IsAllowedStatMode(mode_)) << "invalid mode " << mode;
  }

  // Disallow access to entry that are not S_ISREG |mode_|.
  void CheckEntryIsReg(const char* method) const {
    LOG_IF(FATAL, !S_ISREG(mode_)) << method << " entry not S_IFREG";
  }

  // Node in the InodeTable: not owned.
  Node* node_ = nullptr;

  // Node's associated stat(2).st_mode.
  mode_t mode_ = 0;

  // File data: always empty, except for an S_ISREG entry.
  std::vector<char> data_;
};

FileSystemFake::FileSystemFake() = default;

FileSystemFake::~FileSystemFake() = default;

void FileSystemFake::Init(void* userdata, struct fuse_conn_info*) {
  VLOG(2) << "Init";

  const auto time_now = std::time(nullptr);

  Node* root = GetInodeTable().Lookup(1);
  struct stat root_stat = MakeTimeStat(S_IFDIR | 0777, time_now);
  root_stat = MakeStat(root->ino, root_stat, read_only_);
  GetInodeTable().SetStat(root->ino, root_stat);
  ShowStat(root_stat, root->name);

  files_.insert(FakeFileEntry::Create(root, root_stat.st_mode));
  CHECK_EQ(0, files_.find(root->ino)->second.GetSize());

  const char* file_data = "hello world\r\n";
  const auto data_size = strlen(file_data);

  Node* hello = GetInodeTable().Create(1, "hello");
  struct stat hello_stat = MakeTimeStat(S_IFREG | 0777, time_now);
  hello_stat.st_size = data_size;
  hello_stat = MakeStat(hello->ino, hello_stat, read_only_);
  GetInodeTable().SetStat(hello->ino, hello_stat);
  ShowStat(hello_stat, hello->name);

  files_.insert(FakeFileEntry::Create(hello, hello_stat.st_mode));
  files_.find(hello->ino)->second.SetData(file_data, data_size, 0);
  CHECK_EQ(data_size, files_.find(hello->ino)->second.GetSize());

  CHECK(userdata) << "FileSystem (userdata) is required";
}

void FileSystemFake::Lookup(std::unique_ptr<EntryRequest> request,
                            ino_t parent,
                            const char* name) {
  VLOG(2) << "Lookup parent " << parent << " name " << name;

  if (request->IsInterrupted())
    return;

  Node* node = GetInodeTable().Lookup(parent, name);
  if (!node) {
    request->ReplyError(errno);
    PLOG(ERROR) << "lookup";
    return;
  }

  auto it = files_.find(node->ino);
  if (it == files_.end()) {
    errno = request->ReplyError(ENOENT);
    PLOG(ERROR) << "lookup";
    return;
  }

  struct stat stat;
  CHECK(GetInodeTable().GetStat(node->ino, &stat));
  CHECK_EQ(stat.st_ino, node->ino);

  stat.st_size = it->second.GetSize();
  GetInodeTable().SetStat(node->ino, stat);

  fuse_entry_param entry = {0};
  entry.ino = static_cast<fuse_ino_t>(node->ino);
  entry.attr = stat;
  entry.attr_timeout = kEntryTimeoutSeconds;
  entry.entry_timeout = kEntryTimeoutSeconds;

  request->ReplyEntry(entry);
}

void FileSystemFake::GetAttr(std::unique_ptr<AttrRequest> request, ino_t ino) {
  VLOG(2) << "GetAttr ino " << ino;

  if (request->IsInterrupted())
    return;

  Node* node = GetInodeTable().Lookup(ino);
  if (!node) {
    request->ReplyError(errno);
    PLOG(ERROR) << "getattr";
    return;
  }

  auto it = files_.find(node->ino);
  if (it == files_.end()) {
    errno = request->ReplyError(ENOENT);
    PLOG(ERROR) << "getattr";
    return;
  }

  struct stat stat;
  CHECK(GetInodeTable().GetStat(node->ino, &stat));
  CHECK_EQ(stat.st_ino, node->ino);

  stat.st_size = it->second.GetSize();
  GetInodeTable().SetStat(node->ino, stat);

  const double kStatTimeoutSeconds = 5.0;
  request->ReplyAttr(stat, kStatTimeoutSeconds);
}

void FileSystemFake::SetAttr(std::unique_ptr<AttrRequest> request,
                             ino_t ino,
                             struct stat* attr,
                             int to_set) {
  VLOG(2) << "SetAttr ino " << ino << " fh " << request->fh();

  if (request->IsInterrupted())
    return;

  Node* node = GetInodeTable().Lookup(ino);
  if (!node) {
    request->ReplyError(errno);
    PLOG(ERROR) << "setattr";
    return;
  }

  auto it = files_.find(ino);
  if (it == files_.end()) {
    errno = request->ReplyError(ENOENT);
    PLOG(ERROR) << "setattr";
    return;
  }

  // Allow setting file size ftruncate(2), and file times utime(2).
  const int kAllowedToSet =
      FUSE_SET_ATTR_SIZE | FUSE_SET_ATTR_ATIME | FUSE_SET_ATTR_MTIME;

  constexpr auto allowed_to_set = [](int to_set) {
    // ATTR_XTIME_NOW are optional ATTR_XTIME modifiers: mask them.
    to_set &= ~(FUSE_SET_ATTR_ATIME_NOW | FUSE_SET_ATTR_MTIME_NOW);

    if (to_set & ~kAllowedToSet)
      return ENOTSUP;
    if (!to_set)  // Nothing to_set? error EINVAL.
      return EINVAL;
    return 0;
  };

  VLOG(2) << "to_set " << ToSetFlagsToString(to_set);
  if (errno = allowed_to_set(to_set); errno) {
    request->ReplyError(errno);
    PLOG(ERROR) << "setattr";
    return;
  }

  struct stat stat = {0};
  CHECK(GetInodeTable().GetStat(node->ino, &stat));
  CHECK_EQ(stat.st_ino, node->ino);

  // Set file st_size.
  if (to_set & FUSE_SET_ATTR_SIZE) {
    const auto size = it->second.GetSize();
    stat.st_size = it->second.SetSize(attr->st_size);
    VLOG(2) << "set size " << size << " to " << stat.st_size;
  }

  // Set st_atime || st_mtime.
  if (to_set & (FUSE_SET_ATTR_ATIME | FUSE_SET_ATTR_MTIME)) {
    if (to_set & FUSE_SET_ATTR_ATIME_NOW) {
      stat.st_atime = std::time(nullptr);
      VLOG(2) << "set atime now " << stat.st_atime;
    } else if (to_set & FUSE_SET_ATTR_ATIME) {
      stat.st_atime = attr->st_atime;
      VLOG(2) << "set atime " << stat.st_atime;
    }

    if (to_set & FUSE_SET_ATTR_MTIME_NOW) {
      stat.st_mtime = std::time(nullptr);
      VLOG(2) << "set mtime now " << stat.st_mtime;
    } else if (to_set & FUSE_SET_ATTR_MTIME) {
      stat.st_mtime = attr->st_mtime;
      VLOG(2) << "set mtime " << stat.st_mtime;
    }
  }

  stat.st_size = it->second.GetSize();
  GetInodeTable().SetStat(node->ino, stat);

  const double kStatTimeoutSeconds = 5.0;
  request->ReplyAttr(stat, kStatTimeoutSeconds);
}

void FileSystemFake::MkDir(std::unique_ptr<EntryRequest> request,
                           ino_t parent,
                           const char* name,
                           mode_t mode) {
  VLOG(2) << "MkDir parent " << parent << " name " << name;

  if (request->IsInterrupted())
    return;

  if (read_only_) {
    errno = request->ReplyError(EROFS);
    PLOG(ERROR) << "mkdir";
    return;
  }

  Node* node = GetInodeTable().Create(parent, name);
  if (!node) {
    request->ReplyError(errno);
    PLOG(ERROR) << "mkdir";
    return;
  }

  struct stat stat = MakeTimeStat(S_IFDIR | 0777);
  stat = MakeStat(node->ino, stat, read_only_);
  GetInodeTable().SetStat(node->ino, stat);
  files_.insert(FakeFileEntry::Create(node, stat.st_mode));

  fuse_entry_param entry = {0};
  entry.ino = static_cast<fuse_ino_t>(node->ino);
  entry.attr = stat;
  entry.attr_timeout = kEntryTimeoutSeconds;
  entry.entry_timeout = kEntryTimeoutSeconds;

  request->ReplyEntry(entry);
}

void FileSystemFake::Unlink(std::unique_ptr<OkRequest> request,
                            ino_t parent,
                            const char* name) {
  VLOG(2) << "Unlink parent " << parent << " name " << name;

  if (request->IsInterrupted())
    return;

  Node* node = GetInodeTable().Lookup(parent, name);
  if (!node || node->ino <= FUSE_ROOT_ID) {
    errno = request->ReplyError(!node ? errno : EBUSY);
    PLOG(ERROR) << "unlink";
    return;
  }

  auto it = files_.find(node->ino);
  if (it == files_.end()) {
    errno = request->ReplyError(ENOENT);
    PLOG(ERROR) << "unlink";
    return;
  }

  if (read_only_) {
    errno = request->ReplyError(EROFS);
    PLOG(ERROR) << "unlink";
    return;
  }

  bool removed = GetInodeTable().Forget(node->ino, 1);
  LOG_IF(FATAL, !removed) << "unlink failed";
  files_.erase(it);

  request->ReplyOk();
}

void FileSystemFake::RmDir(std::unique_ptr<OkRequest> request,
                           ino_t parent,
                           const char* name) {
  VLOG(2) << "RmDir parent " << parent << " name " << name;

  if (request->IsInterrupted())
    return;

  Node* node = GetInodeTable().Lookup(parent, name);
  if (!node || node->ino <= FUSE_ROOT_ID) {
    errno = request->ReplyError(!node ? errno : EBUSY);
    PLOG(ERROR) << "rmdir";
    return;
  }

  auto it = files_.find(node->ino);
  if (it == files_.end()) {
    errno = request->ReplyError(ENOENT);
    PLOG(ERROR) << "rmdir";
    return;
  }

  if (read_only_) {
    errno = request->ReplyError(EROFS);
    PLOG(ERROR) << "rmdir";
    return;
  }

  for (const auto& it : files_) {
    Node* child = it.second.node();
    if (!child || child->parent != node->ino)
      continue;  // skip: not a child of the node |ino|.
    errno = request->ReplyError(ENOTEMPTY);
    PLOG(ERROR) << "rmdir";
    return;
  }

  bool removed = GetInodeTable().Forget(node->ino, 1);
  LOG_IF(FATAL, !removed) << "rmdir failed";
  files_.erase(it);

  request->ReplyOk();
}

void FileSystemFake::Rename(std::unique_ptr<OkRequest> request,
                            ino_t parent,
                            const char* name,
                            ino_t new_parent,
                            const char* new_name) {
  VLOG(2) << "Rename parent " << parent << " name " << name;

  if (request->IsInterrupted())
    return;

  Node* node = GetInodeTable().Lookup(parent, name);
  if (!node) {
    request->ReplyError(errno);
    PLOG(ERROR) << "rename";
    return;
  }

  if (!GetInodeTable().Move(node, new_parent, new_name)) {
    request->ReplyError(errno);
    PLOG(ERROR) << "rename";
    return;
  }

  request->ReplyOk();
}

void FileSystemFake::OpenDir(std::unique_ptr<OpenRequest> request, ino_t ino) {
  VLOG(2) << "OpenDir ino " << ino;

  if (request->IsInterrupted())
    return;

  Node* node = GetInodeTable().Lookup(ino);
  if (!node) {
    request->ReplyError(errno);
    PLOG(ERROR) << "opendir";
    return;
  }

  struct stat stat;
  CHECK(GetInodeTable().GetStat(node->ino, &stat));
  CHECK_EQ(stat.st_ino, node->ino);

  if (!S_ISDIR(stat.st_mode)) {
    errno = request->ReplyError(ENOTDIR);
    PLOG(ERROR) << "opendir";
    return;
  }

  VLOG(2) << "flags " << OpenFlagsToString(request->flags());
  if ((request->flags() & O_ACCMODE) != O_RDONLY) {
    errno = request->ReplyError(EPERM);
    PLOG(ERROR) << "opendir";
    return;
  }

  uint64_t handle = fusebox::OpenFile();
  readdir_[handle].reset(new DirEntryResponse(node->ino, handle));

  request->ReplyOpen(handle);
}

void FileSystemFake::ReadDir(std::unique_ptr<DirEntryRequest> request,
                             ino_t ino,
                             off_t off) {
  VLOG(2) << "ReadDir ino " << ino << " off " << off;

  if (request->IsInterrupted())
    return;

  auto it = readdir_.find(request->fh());
  if (it == readdir_.end()) {
    errno = request->ReplyError(EBADF);
    PLOG(ERROR) << "readdir";
    return;
  }

  DirEntryResponse* response = it->second.get();

  constexpr auto dir_entry = [](ino_t ino, const std::string& name) {
    struct stat stat = {0};
    CHECK(GetInodeTable().GetStat(ino, &stat));

    if (ino > FUSE_ROOT_ID && name == "..") {
      Node* node = GetInodeTable().Lookup(ino);
      CHECK(node && GetInodeTable().GetStat(node->parent, &stat));
    }

    return DirEntry{ino, name, stat.st_mode};
  };

  if (off == 0) {
    VLOG(2) << "readdir fh " << request->fh();

    std::vector<DirEntry> entries;
    entries.push_back(dir_entry(ino, "."));
    entries.push_back(dir_entry(ino, ".."));

    for (const auto& it : files_) {
      Node* child = it.second.node();
      if (!child || child->parent != ino)
        continue;  // skip: not a child of the node |ino|.
      const std::string name = child->name.substr(1);
      entries.push_back(dir_entry(child->ino, name));
    }

    for (const auto& entry : entries)
      VLOG(2) << " entry [" << entry.name << "]";
    response->Append(entries, true);
  }

  response->Append(std::move(request));
}

void FileSystemFake::ReleaseDir(std::unique_ptr<OkRequest> request, ino_t ino) {
  VLOG(2) << "ReleaseDir ino " << ino;

  if (request->IsInterrupted())
    return;

  if (!fusebox::GetFile(request->fh())) {
    errno = request->ReplyError(EBADF);
    PLOG(ERROR) << "releasedir";
    return;
  }

  fusebox::CloseFile(request->fh());
  readdir_.erase(request->fh());

  request->ReplyOk();
}

void FileSystemFake::Open(std::unique_ptr<OpenRequest> request, ino_t ino) {
  VLOG(2) << "Open ino " << ino;

  if (request->IsInterrupted())
    return;

  Node* node = GetInodeTable().Lookup(ino);
  if (!node) {
    request->ReplyError(errno);
    PLOG(ERROR) << "open";
    return;
  }

  struct stat stat;
  CHECK(GetInodeTable().GetStat(node->ino, &stat));
  CHECK_EQ(stat.st_ino, node->ino);

  if (S_ISDIR(stat.st_mode)) {
    errno = request->ReplyError(EISDIR);
    PLOG(ERROR) << "open";
    return;
  }

  VLOG(2) << "flags " << OpenFlagsToString(request->flags());
  if (read_only_ && (request->flags() & O_ACCMODE) != O_RDONLY) {
    errno = request->ReplyError(EROFS);
    PLOG(ERROR) << "open";
    return;
  }

  uint64_t handle = fusebox::OpenFile();
  request->ReplyOpen(handle);
}

void FileSystemFake::Read(std::unique_ptr<BufferRequest> request,
                          ino_t ino,
                          size_t size,
                          off_t off) {
  VLOG(2) << "Read ino " << ino << " off " << off << " size " << size;

  if (request->IsInterrupted())
    return;

  if (!fusebox::GetFile(request->fh())) {
    errno = request->ReplyError(EBADF);
    PLOG(ERROR) << "read";
    return;
  }

  struct stat stat;
  CHECK(GetInodeTable().GetStat(ino, &stat));
  CHECK_EQ(stat.st_ino, ino);

  if (S_ISDIR(stat.st_mode)) {
    errno = request->ReplyError(EISDIR);
    PLOG(ERROR) << "read";
    return;
  }

  auto it = files_.find(ino);
  if (it == files_.end()) {
    errno = request->ReplyError(ENOENT);
    PLOG(ERROR) << "read";
    return;
  }

  auto slice = it->second.GetDataSlice(off, size);
  request->ReplyBuffer(slice.data(), slice.size());
}

void FileSystemFake::Write(std::unique_ptr<WriteRequest> request,
                           ino_t ino,
                           const char* buf,
                           size_t size,
                           off_t off) {
  VLOG(2) << "Write ino " << ino << " off " << off << " size " << size;

  if (request->IsInterrupted())
    return;

  if (!fusebox::GetFile(request->fh())) {
    errno = request->ReplyError(EBADF);
    PLOG(ERROR) << "write";
    return;
  }

  struct stat stat;
  CHECK(GetInodeTable().GetStat(ino, &stat));
  CHECK_EQ(stat.st_ino, ino);

  if (S_ISDIR(stat.st_mode)) {
    errno = request->ReplyError(EISDIR);
    PLOG(ERROR) << "write";
    return;
  }

  auto it = files_.find(ino);
  if (it == files_.end()) {
    errno = request->ReplyError(ENOENT);
    PLOG(ERROR) << "write";
    return;
  }

  auto count = it->second.SetData(buf, size, off);
  request->ReplyWrite(count);
}

void FileSystemFake::Release(std::unique_ptr<OkRequest> request, ino_t ino) {
  VLOG(2) << "Release ino " << ino;

  if (request->IsInterrupted())
    return;

  if (!fusebox::GetFile(request->fh())) {
    errno = request->ReplyError(EBADF);
    PLOG(ERROR) << "release";
    return;
  }

  fusebox::CloseFile(request->fh());
  request->ReplyOk();
}

void FileSystemFake::Create(std::unique_ptr<CreateRequest> request,
                            ino_t parent,
                            const char* name,
                            mode_t mode) {
  VLOG(2) << "Create parent " << parent << " name " << name;

  if (request->IsInterrupted())
    return;

  if (!S_ISREG(mode)) {
    errno = request->ReplyError(EINVAL);
    PLOG(ERROR) << "create";
    return;
  }

  if (read_only_) {
    errno = request->ReplyError(EROFS);
    PLOG(ERROR) << "create";
    return;
  }

  Node* node = GetInodeTable().Create(parent, name);
  if (!node) {
    request->ReplyError(errno);
    PLOG(ERROR) << "create";
    return;
  }

  struct stat stat = MakeTimeStat(S_IFREG | 0777);
  stat = MakeStat(node->ino, stat, read_only_);
  GetInodeTable().SetStat(node->ino, stat);
  files_.insert(FakeFileEntry::Create(node, stat.st_mode));

  fuse_entry_param entry = {0};
  entry.ino = static_cast<fuse_ino_t>(node->ino);
  entry.attr = stat;
  entry.attr_timeout = kEntryTimeoutSeconds;
  entry.entry_timeout = kEntryTimeoutSeconds;

  VLOG(2) << "flags " << OpenFlagsToString(request->flags());
  uint64_t handle = fusebox::OpenFile();

  request->ReplyCreate(entry, handle);
}

}  // namespace fusebox
