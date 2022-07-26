// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_MESSAGE_DISPATCHER_H_
#define PATCHPANEL_MESSAGE_DISPATCHER_H_

#include <memory>
#include <string>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/scoped_file.h>
#include <base/memory/weak_ptr.h>

#include "patchpanel/ipc.pb.h"

namespace patchpanel {

// Helper message processor
class MessageDispatcher {
 public:
  explicit MessageDispatcher(base::ScopedFD fd, bool start = true);
  MessageDispatcher(const MessageDispatcher&) = delete;
  MessageDispatcher& operator=(const MessageDispatcher&) = delete;

  void Start();

  void RegisterFailureHandler(base::RepeatingCallback<void()> handler);

  void RegisterMessageHandler(
      base::RepeatingCallback<void(const SubprocessMessage&)> handler);

  void SendMessage(const SubprocessMessage& proto) const;

 private:
  // Overrides MessageLoopForIO callbacks for new data on |control_fd_|.
  void OnFileCanReadWithoutBlocking();

  base::ScopedFD fd_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher_;
  base::RepeatingCallback<void()> failure_handler_;
  base::RepeatingCallback<void(const SubprocessMessage&)> message_handler_;

  SubprocessMessage msg_;

  base::WeakPtrFactory<MessageDispatcher> weak_factory_{this};
};

}  // namespace patchpanel

#endif  // PATCHPANEL_MESSAGE_DISPATCHER_H_
