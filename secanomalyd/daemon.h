// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECANOMALYD_DAEMON_H_
#define SECANOMALYD_DAEMON_H_

#include <map>

#include <brillo/daemons/daemon.h>

#include "secanomalyd/mount_entry.h"

class Daemon : public brillo::Daemon {
 public:
  Daemon() : brillo::Daemon() {}
  Daemon(const Daemon&) = delete;
  Daemon& operator=(const Daemon&) = delete;

 protected:
  int OnEventLoopStarted() override;

 private:
  void CheckRwMounts();
  void DoRwMountCheck();

  std::map<base::FilePath, MountEntry> wx_mounts_;
};

#endif  // SECANOMALYD_DAEMON_H_
