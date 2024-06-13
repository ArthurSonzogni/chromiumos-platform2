// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_TC_PROCESS_H_
#define SHILL_NETWORK_TC_PROCESS_H_

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/memory/weak_ptr.h>
#include <chromeos/net-base/process_manager.h>

namespace shill {

// This class represents a 'tc' process spawned in a minijail, and is used to
// write commands to its stdin.
class TCProcess {
 public:
  using ExitCallback = net_base::ProcessManager::ExitCallback;

  static constexpr std::string_view kTCUser = "nobody";
  static constexpr std::string_view kTCGroup = "nobody";
  static constexpr std::string_view kTCPath = "/sbin/tc";

  // Spawns a TC process in minijail and writes |commands| to the process. Once
  // the process is exited, |exit_callback| will be called with the status.
  // If the instance is destroyed before the process is exited, |exit_callback|
  // won't be executed.
  static std::unique_ptr<TCProcess> Create(
      const std::vector<std::string>& commands,
      ExitCallback exit_callback,
      net_base::ProcessManager* process_manager =
          net_base::ProcessManager::GetInstance());

  virtual ~TCProcess();

 protected:
  TCProcess(net_base::ProcessManager* process_manager,
            const std::vector<std::string>& commands,
            ExitCallback exit_callback);

 private:
  bool Initialize();
  void OnTCProcessWritable();
  void OnProcessExited(int exit_status);

  net_base::ProcessManager* process_manager_;
  std::vector<std::string> commands_;
  ExitCallback exit_callback_;

  pid_t tc_pid_ = net_base::ProcessManager::kInvalidPID;

  // The fd of TC process's stdin. It only contains value until the TC process
  // able to send commands via it.
  base::ScopedFD tc_stdin_;
  //  Watcher to wait for |tc_stdin_| ready to write. It should be
  //  destructed prior than |tc_stdin_| is closed.
  std::unique_ptr<base::FileDescriptorWatcher::Controller> tc_stdin_watcher_;

  base::WeakPtrFactory<TCProcess> weak_factory_{this};
};

// Creates the TCProcess instance. It's used for injecting MockTCProcessFactory
// at testing to create the mock TCProcess instance.
class TCProcessFactory {
 public:
  TCProcessFactory() = default;
  virtual ~TCProcessFactory() = default;

  virtual std::unique_ptr<TCProcess> Create(
      const std::vector<std::string>& commands,
      TCProcess::ExitCallback exit_callback,
      net_base::ProcessManager* process_manager =
          net_base::ProcessManager::GetInstance());
};

}  // namespace shill
#endif  // SHILL_NETWORK_TC_PROCESS_H_
