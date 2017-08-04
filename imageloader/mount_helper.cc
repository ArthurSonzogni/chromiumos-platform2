// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mount_helper.h"

#include <fcntl.h>
#include <libminijail.h>
#include <scoped_minijail.h>
#include <sys/capability.h>
#include <sys/socket.h>
#include <unistd.h>

#include <base/files/file_path.h>
#include <base/bind.h>
#include <base/logging.h>
#include <base/time/time.h>

#include "imageloader.h"
#include "ipc.pb.h"
#include "verity_mounter.h"

namespace imageloader {

namespace {
constexpr char kSeccompFilterPath[] =
    "/opt/google/imageloader/imageloader-helper-seccomp.policy";
}  // namespace

MountHelper::MountHelper(base::ScopedFD control_fd)
    : control_fd_(std::move(control_fd)),
      control_watcher_(),
      pending_fd_(-1),
      mounter_() {}

int MountHelper::OnInit() {
  // Prevent the main process from sending us any signals.
  // errno can be EPERM if the process is already the group leader.
  if (setsid() < 0 && errno != EPERM) PLOG(FATAL) << "setsid failed";

  // Run with minimal privileges.
  ScopedMinijail jail(minijail_new());
  minijail_no_new_privs(jail.get());
  minijail_use_seccomp_filter(jail.get());
  minijail_parse_seccomp_filters(jail.get(), kSeccompFilterPath);
  minijail_reset_signal_mask(jail.get());
  minijail_namespace_net(jail.get());
  minijail_skip_remount_private(jail.get());
  minijail_enter(jail.get());

  MessageLoopForIO::current()->WatchFileDescriptor(control_fd_.get(), true,
                                                   MessageLoopForIO::WATCH_READ,
                                                   &control_watcher_, this);
  return Daemon::OnInit();
}

void MountHelper::OnFileCanReadWithoutBlocking(int fd) {
  CHECK_EQ(fd, control_fd_.get());
  char buffer[4096 * 4];
  memset(buffer, '\0', sizeof(buffer));

  struct msghdr msg = {0};
  struct iovec iov[1];

  iov[0].iov_base = buffer;
  iov[0].iov_len = sizeof(buffer);

  msg.msg_iov = iov;
  msg.msg_iovlen = sizeof(iov) / sizeof(iov[0]);

  char c_buffer[256];
  msg.msg_control = c_buffer;
  msg.msg_controllen = sizeof(c_buffer);

  ssize_t bytes = recvmsg(fd, &msg, 0);
  if (bytes < 0) PLOG(FATAL) << "recvmsg failed";
  // Per recvmsg(2), the return value will be 0 when the peer has performed an
  // orderly shutdown.
  if (bytes == 0) _exit(0);

  struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);

  if (cmsg == nullptr) LOG(FATAL) << "no cmsg";

  if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
    LOG(FATAL) << "cmsg is wrong type";

  memmove(&pending_fd_, CMSG_DATA(cmsg), sizeof(pending_fd_));

  MountImage command;
  if (!command.ParseFromArray(buffer, strlen(buffer)))
    LOG(FATAL) << "error parsing protobuf";

  // Handle the command to mount the image.
  MountResponse response = HandleCommand(command);
  // Reply to the parent process with the success or failure.
  SendResponse(response);
}

MountResponse MountHelper::HandleCommand(MountImage& command) {
  // Convert the fs type to a string.
  std::string fs_type;
  switch (command.fs_type()) {
    case MountImage_FileSystem_EXT4:
      fs_type = "ext4";
      break;
    case MountImage_FileSystem_SQUASH:
      fs_type = "squashfs";
      break;
    default:
      LOG(FATAL) << "unknown filesystem type";
  }

  bool status = mounter_.Mount(base::ScopedFD(pending_fd_),
                               base::FilePath(command.mount_path()),
                               fs_type,
                               command.table());
  if (!status) LOG(ERROR) << "mount failed";

  MountResponse response;
  response.set_success(status);
  return response;
}

void MountHelper::SendResponse(MountResponse& response) {
  std::string response_str;
  if (!response.SerializeToString(&response_str))
    LOG(FATAL) << "failed to serialize protobuf";

  if (write(control_fd_.get(), response_str.data(), response_str.size()) !=
      static_cast<ssize_t>(response_str.size())) {
    LOG(FATAL) << "short write on protobuf";
  }
}

}  // namespace imageloader
