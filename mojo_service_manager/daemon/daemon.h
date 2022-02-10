// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SERVICE_MANAGER_DAEMON_DAEMON_H_
#define MOJO_SERVICE_MANAGER_DAEMON_DAEMON_H_

#include <memory>

#include <base/threading/thread.h>
#include <brillo/daemons/daemon.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

namespace chromeos {
namespace mojo_service_manager {

class Daemon : public ::brillo::Daemon {
 public:
  Daemon();
  Daemon(const Daemon&) = delete;
  Daemon& operator=(const Daemon&) = delete;
  ~Daemon() override;

 protected:
  // ::brillo::Daemon override.
  int OnEventLoopStarted() override;

 private:
  // The thread for mojo io.
  base::Thread mojo_thread_;
  // The task runner to post tasks to the mojo thread.
  scoped_refptr<base::SingleThreadTaskRunner> mojo_task_runner_;
  // Sets task runner for mojo api.
  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;
};
}  // namespace mojo_service_manager
}  // namespace chromeos

#endif  // MOJO_SERVICE_MANAGER_DAEMON_DAEMON_H_
