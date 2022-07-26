// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/message_dispatcher.h"

#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/posix/unix_domain_socket.h>

namespace patchpanel {

MessageDispatcher::MessageDispatcher(base::ScopedFD fd, bool start)
    : fd_(std::move(fd)) {
  if (start)
    Start();
}

void MessageDispatcher::Start() {
  watcher_ = base::FileDescriptorWatcher::WatchReadable(
      fd_.get(),
      base::BindRepeating(&MessageDispatcher::OnFileCanReadWithoutBlocking,
                          base::Unretained(this)));
}

void MessageDispatcher::RegisterFailureHandler(
    base::RepeatingCallback<void()> handler) {
  failure_handler_ = std::move(handler);
}

void MessageDispatcher::RegisterMessageHandler(
    base::RepeatingCallback<void(const SubprocessMessage&)> handler) {
  message_handler_ = std::move(handler);
}

void MessageDispatcher::OnFileCanReadWithoutBlocking() {
  char buffer[1024];
  std::vector<base::ScopedFD> fds{};
  ssize_t len =
      base::UnixDomainSocket::RecvMsg(fd_.get(), buffer, sizeof(buffer), &fds);

  if (len <= 0) {
    PLOG(ERROR) << "Read failed: exiting";
    watcher_.reset();
    if (!failure_handler_.is_null())
      failure_handler_.Run();
    return;
  }

  msg_.Clear();
  if (!msg_.ParseFromArray(buffer, len)) {
    LOG(ERROR) << "Error parsing protobuf";
    return;
  }

  if (!message_handler_.is_null()) {
    message_handler_.Run(msg_);
  }
}

void MessageDispatcher::SendMessage(const SubprocessMessage& proto) const {
  std::string str;
  if (!proto.SerializeToString(&str)) {
    LOG(ERROR) << "error serializing protobuf";
  }
  if (write(fd_.get(), str.data(), str.size()) !=
      static_cast<ssize_t>(str.size())) {
    LOG(ERROR) << "short write on protobuf";
  }
}

}  // namespace patchpanel
