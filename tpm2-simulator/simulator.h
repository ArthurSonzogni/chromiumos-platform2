// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TPM2_SIMULATOR_SIMULATOR_H_
#define TPM2_SIMULATOR_SIMULATOR_H_

#include <memory>
#include <string>

#include <base/files/file.h>
#include <brillo/daemons/daemon.h>

namespace tpm2_simulator {

class SimulatorDaemon final : public brillo::Daemon {
 public:
  SimulatorDaemon() = default;
  SimulatorDaemon(const SimulatorDaemon&) = delete;
  SimulatorDaemon& operator=(const SimulatorDaemon&) = delete;
  ~SimulatorDaemon() = default;

 protected:
  int OnInit() override;
  void OnCommand();
  std::string remain_request_;
  base::ScopedFD command_fd_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> command_fd_watcher_;
};

}  // namespace tpm2_simulator

#endif  // TPM2_SIMULATOR_SIMULATOR_H_
