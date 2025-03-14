// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_VSH_VSH_FORWARDER_H_
#define VM_TOOLS_VSH_VSH_FORWARDER_H_

#include <pwd.h>
#include <sys/types.h>

#include <array>
#include <memory>
#include <string>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/scoped_file.h>
#include <brillo/asynchronous_signal_handler.h>
#include <google/protobuf/message_lite.h>
#include <vm_protos/proto_bindings/vsh.pb.h>

#include "vm_tools/vsh/scoped_termios.h"

namespace vm_tools::vsh {

// VshForwarder encapsulates a vsh forwarder session.
// This class is not thread-safe.
class VshForwarder {
 public:
  static std::unique_ptr<VshForwarder> Create(base::ScopedFD sock_fd,
                                              bool inherit_env,
                                              std::string default_user,
                                              bool allow_to_switch_user);
  ~VshForwarder() = default;

 private:
  VshForwarder(base::ScopedFD sock_fd,
               bool inherit_env,
               std::string default_user,
               bool allow_to_switch_user);
  VshForwarder(const VshForwarder&) = delete;
  VshForwarder& operator=(const VshForwarder&) = delete;

  bool Init();

  bool HandleSigchld(const struct signalfd_siginfo& siginfo);
  void HandleVsockReadable();
  void HandleTargetReadable(int fd, StdioStream stream_type);

  bool SendConnectionResponse(vm_tools::vsh::ConnectionStatus status,
                              const std::string& description);
  void PrepareExec(
      const char* pts,
      const struct passwd* passwd,
      const vm_tools::vsh::SetupConnectionRequest& connection_request);

  void SendExitMessage();

  void SendAllData(int fd, StdioStream stream_type);

  // FDs (`stdio_pipes_`, `ptm_fd_` and `sock_fd_`) must be declared before
  // watchers (`stdout_watcher_`, `stderr_watcher_` and `socket_watcher_`)
  // because the formers need to outlive the latters.
  std::array<base::ScopedFD, 3> stdio_pipes_;
  base::ScopedFD ptm_fd_;
  base::ScopedFD sock_fd_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> socket_watcher_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> stdout_watcher_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> stderr_watcher_;
  bool inherit_env_;
  bool interactive_ = true;

  brillo::AsynchronousSignalHandler signal_handler_;

  pid_t target_pid_;
  int exit_code_;
  const std::string default_user_;
  const bool allow_to_switch_user_;
};

}  // namespace vm_tools::vsh

#endif  // VM_TOOLS_VSH_VSH_FORWARDER_H_
