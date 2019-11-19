// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/vm/vsock_proxy/fuse_mount.h"

#include <fuse/fuse.h>
#include <fuse/fuse_lowlevel.h>
#include <fuse/fuse_opt.h>

#include <vector>

#include <base/bind.h>

namespace arc {

namespace {

// Calls fuse_mount() to create a channel.
struct fuse_chan* Mount(const base::FilePath& mount_path,
                        const std::string& name) {
  const std::string subtype_option = "-osubtype=" + name;
  const char* argv[] = {
      "",  // Dummy argv[0],
      subtype_option.c_str(),
  };
  struct fuse_args args =
      FUSE_ARGS_INIT(arraysize(argv), const_cast<char**>(argv));
  auto* channel = fuse_mount(mount_path.value().c_str(), &args);
  fuse_opt_free_args(&args);
  return channel;
}

}  // namespace

FuseMount::FuseMount(const base::FilePath& mount_path, const std::string& name)
    : mount_path_(mount_path), name_(name) {}

FuseMount::~FuseMount() {
  if (channel_)
    fuse_unmount(mount_path_.value().c_str(), channel_);
  if (fuse_)
    fuse_destroy(fuse_);
}

bool FuseMount::Init(int argc,
                     char* argv[],
                     const struct fuse_operations& operations,
                     void* private_data) {
  // Initialize fuse channel.
  channel_ = Mount(mount_path_, name_);
  if (!channel_) {
    LOG(ERROR) << "Failed to mount at: " << mount_path_.value();
    return false;
  }
  buf_.resize(fuse_chan_bufsize(channel_));
  // Initialize fuse object.
  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  fuse_ =
      fuse_new(channel_, &args, &operations, sizeof(operations), private_data);
  fuse_opt_free_args(&args);
  if (!fuse_) {
    LOG(ERROR) << "fuse_new() failed.";
    return false;
  }
  // Start watching the channel FD.
  watcher_ = base::FileDescriptorWatcher::WatchReadable(
      fuse_chan_fd(channel_), base::BindRepeating(&FuseMount::OnChannelReadable,
                                                  base::Unretained(this)));
  return true;
}

void FuseMount::OnChannelReadable() {
  struct fuse_buf fbuf = {
      .size = buf_.size(),
      .mem = buf_.data(),
  };
  struct fuse_session* session = fuse_get_session(fuse_);
  int result = fuse_session_receive_buf(session, &fbuf, &channel_);
  if (result <= 0) {
    if (result == -EINTR) {
      // Not a serious error. Return to retry.
      return;
    }
    if (result == 0) {
      LOG(INFO) << "File system exited.";
    } else {
      LOG(ERROR) << "fuse_session_receive_buf() failed: " << result;
    }
    watcher_.reset();  // Stop watching the channel FD.
    return;
  }
  fuse_session_process_buf(session, &fbuf, channel_);
}

}  // namespace arc
