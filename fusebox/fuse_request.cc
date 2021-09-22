// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fusebox/fuse_request.h"

#include <base/check.h>
#include <base/check_op.h>
#include <base/strings/string_piece.h>

namespace fusebox {

FuseRequest::FuseRequest(fuse_req_t req) : req_(req) {}

bool FuseRequest::IsInterrupted() const {  // Kernel FUSE interrupt
  return fuse_req_interrupted(req_);
}

FuseRequest::~FuseRequest() {
  if (!replied_)
    fuse_reply_err(req_, EINTR);  // User-space FUSE interrupt
}

int FuseRequest::ReplyError(int error) {
  DCHECK(!replied_);
  DCHECK_GT(error, 0);
  fuse_reply_err(req_, error);
  replied_ = true;
  return error;
}

void OkRequest::ReplyOk() {
  DCHECK(!replied_);
  fuse_reply_err(req_, 0);
  replied_ = true;
}

void NoneRequest::ReplyNone() {
  DCHECK(!replied_);
  fuse_reply_none(req_);
  replied_ = true;
}

void AttrRequest::ReplyAttr(const struct stat& attr, double timeout) {
  DCHECK(!replied_);
  fuse_reply_attr(req_, &attr, timeout);
  replied_ = true;
}

void EntryRequest::ReplyEntry(const fuse_entry_param& entry) {
  DCHECK(!replied_);
  fuse_reply_entry(req_, &entry);
  replied_ = true;
}

void OpenRequest::ReplyOpen(uint64_t fh) {
  DCHECK(!replied_);
  DCHECK_NE(0, fh);
  fuse_file_info fi = {0};
  fi.fh = fh;
  fuse_reply_open(req_, &fi);
  replied_ = true;
}

void CreateRequest::ReplyCreate(const fuse_entry_param& entry, uint64_t fh) {
  DCHECK(!replied_);
  DCHECK_NE(0, fh);
  fuse_file_info fi = {0};
  fi.fh = fh;
  fuse_reply_create(req_, &entry, &fi);
  replied_ = true;
}

void BufferRequest::ReplyBuffer(const char* buf, size_t length) {
  DCHECK(!replied_);
  replied_ = true;

  if (buf) {
    fuse_reply_buf(req_, buf, length);
  } else {
    fuse_reply_buf(req_, nullptr, 0);
  }
}

void WriteRequest::ReplyWrite(size_t count) {
  DCHECK(!replied_);
  fuse_reply_write(req_, count);
  replied_ = true;
}

DirEntryRequest::DirEntryRequest(fuse_req_t req,
                                 fuse_ino_t ino,
                                 size_t size,
                                 off_t off)
    : FuseRequest(req), parent_(ino), size_(size), offset_(off) {
  DCHECK(size_);
}

bool DirEntryRequest::AddEntry(DirEntry entry, off_t offset) {
  DCHECK(!replied_);

  const char* name = entry.name;
  struct stat stat = {0};
  stat.st_ino = entry.ino;
  stat.st_mode = entry.mode;

  if (!buf_.get()) {
    buf_ = std::make_unique<char[]>(size_);
    CHECK(buf_.get());
    off_ = 0;
  }

  size_t size = size_ - off_;
  if (fuse_add_direntry(req_, nullptr, 0, name, nullptr, 0) > size)
    return false;  // no |buf_| space.

  char* data = buf_.get() + off_;
  off_ += fuse_add_direntry(req_, data, size, name, &stat, offset);
  CHECK_LE(off_, size_);
  return true;
}

void DirEntryRequest::ReplyDone() {
  DCHECK(!replied_);
  fuse_reply_buf(req_, buf_.get(), off_);
  replied_ = true;
}

}  // namespace fusebox
