// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECANOMALYD_DAEMON_H_
#define SECANOMALYD_DAEMON_H_

#include <memory>

#include <base/files/file_path.h>

#include <brillo/daemons/dbus_daemon.h>

#include "secanomalyd/audit_log_reader.h"
#include "secanomalyd/mount_entry.h"
#include "secanomalyd/mounts.h"
#include "secanomalyd/processes.h"
#include "secanomalyd/system_context.h"

namespace secanomalyd {

class Daemon : public brillo::DBusDaemon {
 public:
  explicit Daemon(bool generate_reports = false,
                  bool forbidden_intersection_only_reports = false,
                  bool dev = false)
      : brillo::DBusDaemon(),
        generate_reports_{generate_reports},
        forbidden_intersection_only_reports_(
            forbidden_intersection_only_reports),
        dev_{dev} {}
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

  // Anomaly detection tasks below check for specific anomalous conditions and
  // record any discovered anomalies.
  void DoWXMountScan();
  void DoProcScan();
  void DoAuditLogScan();

  // This function has built-in rate limiting criteria for uploading reports.
  void DoAnomalousSystemReporting();

  // Discovered anomalies and other security related metrics are reported to UMA
  // at set intervals, dictated by |kUmaReportInterval|.
  void ReportUmaMetrics();

  // UMA Reporting tasks are invoked by |ReportUmaMetrics()|.
  void EmitWXMountCountUma();
  void EmitForbiddenIntersectionProcCountUma();
  void EmitMemfdExecProcCountUma();
  void EmitSandboxingUma();

  // These booleans track whether the daemon has ever attempted to generate a
  // crash report due to an anomaly. Attempts are capped at 1 per anomaly type.
  bool has_attempted_wx_mount_report_ = false;
  bool has_attempted_forbidden_intersection_report_ = false;
  bool has_attempted_memfd_exec_report_ = false;

  // Forbidden intersection process count is sent once per boot.
  bool has_emitted_forbidden_intersection_uma_ = false;

  // Used to track whether an UMA metric was emitted for the memfd execution
  // baseline metric, as we only need one emission of the metric.
  bool has_emitted_memfd_baseline_uma_ = false;

  // Landlock status should only be reported once per execution of secanomalyd,
  // as a change in the Landlock state would require a system reboot.
  bool has_emitted_landlock_status_uma_ = false;

  // Following sandboxing metrics are sent only once per execution of
  // secanomalyd and only in the logged-in state.
  bool has_emitted_seccomp_coverage_uma_ = false;
  bool has_emitted_nnp_proc_percentage_uma_ = false;
  bool has_emitted_nonroot_proc_percentage_uma_ = false;
  bool has_emitted_unpriv_proc_percentage_uma_ = false;
  bool has_emitted_non_initns_proc_percentage_uma_ = false;

  bool generate_reports_ = false;
  bool forbidden_intersection_only_reports_ = false;
  bool dev_ = false;

  std::unique_ptr<SessionManagerProxy> session_manager_proxy_;

  std::unique_ptr<SystemContext> system_context_;

  MountEntryMap wx_mounts_;
  MaybeMountEntries all_mounts_;
  MaybeProcEntries forbidden_intersection_procs_;
  MaybeProcEntries all_procs_;
  MaybeProcEntry init_proc_;

  FilePaths executables_attempting_memfd_exec_;

  // Used for reading and parsing the audit log file.
  std::unique_ptr<AuditLogReader> audit_log_reader_;
};

}  // namespace secanomalyd

#endif  // SECANOMALYD_DAEMON_H_
