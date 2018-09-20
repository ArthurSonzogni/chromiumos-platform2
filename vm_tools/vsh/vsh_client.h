// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_VSH_VSH_CLIENT_H_
#define VM_TOOLS_VSH_VSH_CLIENT_H_

#include <memory>
#include <string>

#include <base/files/scoped_file.h>
#include <base/macros.h>
#include <brillo/asynchronous_signal_handler.h>
#include <brillo/message_loops/message_loop.h>
#include <google/protobuf/message_lite.h>

#include "vm_tools/vsh/scoped_termios.h"

namespace vm_tools {
namespace vsh {

// VshClient encapsulates a vsh client session.
class VshClient {
 public:
  static std::unique_ptr<VshClient> Create(base::ScopedFD sock_fd,
                                           const std::string& user,
                                           const std::string& container);
  ~VshClient() = default;

  int exit_code();

 private:
  explicit VshClient(base::ScopedFD sock_fd);

  bool Init(const std::string& user, const std::string& container);

  bool HandleTermSignal(const struct signalfd_siginfo& siginfo);
  bool HandleWindowResizeSignal(const struct signalfd_siginfo& siginfo);
  void HandleVsockReadable();
  void HandleStdinReadable();
  bool SendCurrentWindowSize();

  base::ScopedFD sock_fd_;
  brillo::MessageLoop::TaskId stdin_task_;

  brillo::AsynchronousSignalHandler signal_handler_;

  int exit_code_;

  DISALLOW_COPY_AND_ASSIGN(VshClient);
};

}  // namespace vsh
}  // namespace vm_tools

#endif  // VM_TOOLS_VSH_VSH_CLIENT_H_
