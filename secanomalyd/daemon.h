// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECANOMALYD_DAEMON_H_
#define SECANOMALYD_DAEMON_H_

#include <map>

#include <brillo/daemons/daemon.h>

#include "secanomalyd/mount_entry.h"

namespace secanomalyd {

class Daemon : public brillo::Daemon {
 public:
  explicit Daemon(bool generate_reports = false, bool dev = false)
      : brillo::Daemon(), generate_reports_{generate_reports}, dev_{dev} {}
  Daemon(const Daemon&) = delete;
  Daemon& operator=(const Daemon&) = delete;

 protected:
  int OnEventLoopStarted() override;

 private:
  void CheckWXMounts();
  void DoWXMountCheck();

  void ReportWXMountCount();
  void DoWXMountCountReporting();

  bool generate_reports_ = false;
  bool dev_ = false;

  MountEntryMap wx_mounts_;
};

}  // namespace secanomalyd

#endif  // SECANOMALYD_DAEMON_H_
