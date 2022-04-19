// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECANOMALYD_DAEMON_H_
#define SECANOMALYD_DAEMON_H_

#include <map>
#include <memory>

#include <brillo/daemons/dbus_daemon.h>

#include "secanomalyd/mount_entry.h"

namespace secanomalyd {

class Daemon : public brillo::DBusDaemon {
 public:
  explicit Daemon(bool generate_reports = false, bool dev = false)
      : brillo::DBusDaemon(), generate_reports_{generate_reports}, dev_{dev} {}
  Daemon(const Daemon&) = delete;
  Daemon& operator=(const Daemon&) = delete;

 protected:
  int OnInit() override;
  int OnEventLoopStarted() override;

 private:
  void CheckWXMounts();
  void DoWXMountCheck();

  void ReportWXMountCount();
  void DoWXMountCountReporting();

  bool generate_reports_ = false;
  bool dev_ = false;

  bool has_reported_ = false;

  std::unique_ptr<SessionManagerProxy> session_manager_proxy_;

  MountEntryMap wx_mounts_;
};

}  // namespace secanomalyd

#endif  // SECANOMALYD_DAEMON_H_
