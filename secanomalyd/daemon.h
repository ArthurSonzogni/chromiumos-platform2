// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECANOMALYD_DAEMON_H_
#define SECANOMALYD_DAEMON_H_

#include <map>
#include <memory>

#include <brillo/daemons/dbus_daemon.h>

#include "secanomalyd/audit_log_reader.h"
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
  void InitAuditLogReader();

  // This is called at set intervals, dictated by |kScanInterval| and invokes
  // all the anomaly detection tasks one by one.
  void ScanForAnomalies();

  // Anomaly detection tasks.
  void DoWXMountScan();
  void DoAuditLogScan();

  // Discovered anomalies are reported at set intervals, dictated by
  // |kReportInterval|.
  void ReportAnomalies();

  // Anomaly reporting tasks.
  void DoWXMountCountReporting();

  // Used to keep track of whether this daemon has attempted to send a crash
  // report for a W+X mount observation throughout its lifetime.
  bool has_attempted_wx_mount_report_ = false;

  bool generate_reports_ = false;
  bool dev_ = false;

  std::unique_ptr<SessionManagerProxy> session_manager_proxy_;

  MountEntryMap wx_mounts_;

  // Used for reading and parsing the audit log file.
  std::unique_ptr<AuditLogReader> audit_log_reader_;
};

}  // namespace secanomalyd

#endif  // SECANOMALYD_DAEMON_H_
