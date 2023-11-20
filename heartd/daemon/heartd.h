// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEARTD_DAEMON_HEARTD_H_
#define HEARTD_DAEMON_HEARTD_H_

#include <brillo/daemons/daemon.h>

namespace heartd {

class HeartdDaemon final : public brillo::Daemon {
 public:
  HeartdDaemon();
  HeartdDaemon(const HeartdDaemon&) = delete;
  HeartdDaemon& operator=(const HeartdDaemon&) = delete;
  ~HeartdDaemon() override;
};

}  // namespace heartd

#endif  // HEARTD_DAEMON_HEARTD_H_
