// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_SUBPROCESS_CONTROLLER_H_
#define PATCHPANEL_SUBPROCESS_CONTROLLER_H_

#include <sys/types.h>

#include <memory>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/files/scoped_file.h>
#include <base/memory/weak_ptr.h>

#include "patchpanel/ipc.pb.h"
#include "patchpanel/message_dispatcher.h"

namespace patchpanel {

// Tracks a helper subprocess.  Handles forking, cleaning up on termination,
// and IPC.
// This object is used by the main Manager process.
class SubprocessController {
 public:
  SubprocessController() = default;
  SubprocessController(const SubprocessController&) = delete;
  SubprocessController& operator=(const SubprocessController&) = delete;

  virtual ~SubprocessController() = default;

  // Re-execs patchpanel with a new argument: "|fd_arg|=N", where N is the
  // side of |control_fd|.  This tells the subprocess to start up a different
  // mainloop.
  void Start(int argc, char* argv[], const std::string& fd_arg);

  // Attempts to restart the process with the original arguments.
  // Returns false if the maximum number of restarts has been exceeded.
  bool Restart();

  // Serializes a protobuf and sends it to the helper process.
  void SendControlMessage(const ControlMessage& proto) const;

  // Start listening on messages from subprocess and dispatching them to
  // handlers. This function can only be called after that the message loop of
  // main process is initialized.
  void Listen();

  void RegisterFeedbackMessageHandler(
      base::RepeatingCallback<void(const FeedbackMessage&)> handler);

  pid_t pid() const { return pid_; }
  uint8_t restarts() const { return restarts_; }

 private:
  void Launch();
  void OnMessage(const SubprocessMessage&);

  base::RepeatingCallback<void(const FeedbackMessage&)> feedback_handler_;

  pid_t pid_{0};
  uint8_t restarts_{0};
  std::vector<std::string> argv_;
  std::string fd_arg_;
  std::unique_ptr<MessageDispatcher> msg_dispatcher_;

  base::WeakPtrFactory<SubprocessController> weak_factory_{this};
};

}  // namespace patchpanel

#endif  // PATCHPANEL_SUBPROCESS_CONTROLLER_H_
