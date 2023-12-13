// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEARTD_DAEMON_HEARTD_H_
#define HEARTD_DAEMON_HEARTD_H_

#include <memory>

#include <brillo/daemons/daemon.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

#include "heartd/daemon/mojo_service.h"

namespace heartd {

class HeartdDaemon final : public brillo::Daemon {
 public:
  HeartdDaemon();
  HeartdDaemon(const HeartdDaemon&) = delete;
  HeartdDaemon& operator=(const HeartdDaemon&) = delete;
  ~HeartdDaemon() override;

 private:
  // For mojo thread initialization.
  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;
  // Used to provide mojo interface to mojo service manager.
  std::unique_ptr<HeartdMojoService> mojo_service_ = nullptr;
};

}  // namespace heartd

#endif  // HEARTD_DAEMON_HEARTD_H_
