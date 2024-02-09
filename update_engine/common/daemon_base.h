// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_DAEMON_BASE_H_
#define UPDATE_ENGINE_COMMON_DAEMON_BASE_H_

#include <memory>

#include <brillo/daemons/daemon.h>

namespace chromeos_update_engine {

class DaemonBase : public brillo::Daemon {
 public:
  DaemonBase() = default;
  DaemonBase(const DaemonBase&) = delete;
  DaemonBase& operator=(const DaemonBase&) = delete;

  virtual ~DaemonBase() = default;

  // Creates an instance of the daemon.
  static std::unique_ptr<DaemonBase> CreateInstance();
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_DAEMON_BASE_H_
