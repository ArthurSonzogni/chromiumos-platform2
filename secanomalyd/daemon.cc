// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secanomalyd/daemon.h"

#include <sys/types.h>
#include <sysexits.h>

#include <memory>
#include <string>

#include <absl/strings/match.h>
#include <base/command_line.h>
#include <base/files/file_util.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/rand_util.h>
#include <base/strings/string_piece.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>

#include <brillo/process/process.h>
#include <brillo/message_loops/message_loop.h>

#include "secanomalyd/audit_log_reader.h"
#include "secanomalyd/metrics.h"
#include "secanomalyd/mount_entry.h"
#include "secanomalyd/mounts.h"
#include "secanomalyd/processes.h"
#include "secanomalyd/reporter.h"
#include "secanomalyd/system_context.h"

namespace secanomalyd {

namespace {

// Sets the sampling frequency for W+X mount count uploads, such that the
// systems with more W+X mounts are more likely to send a crash report, in
// addition to limiting the total number of uploaded reports.
constexpr int CalculateSampleFrequency(size_t wx_mount_count) {
  if (wx_mount_count <= 5)
    return 15;
  else if (wx_mount_count <= 10)
    return 10;
  else if (wx_mount_count <= 15)
    return 5;
  else
    return 2;
}
constexpr int kProcAnomalySampleFrequency = 10000000;  // 10 million

constexpr base::TimeDelta kScanInterval = base::Seconds(30);
// Used to limit the total number of UMA reports.
// Per Platform.DailyUseTime histogram this interval should ensure that enough
// users run the reporting.
constexpr base::TimeDelta kUmaReportInterval = base::Hours(2);

// Generates a unique name for the next element being added to `set`, where the
// element is a unique instance of a certain path type denoted by a `prefix`.
// For example, unknown executable paths are recorded as:
// {"unknown_executable_1", "unknown_executable_2", etc...}
std::string GetNextUniquePath(const FilePaths& set, const std::string& prefix) {
  int num_common_elements = 0;
  for (base::FilePath element : set) {
    if (absl::StartsWith(element.value(), prefix))
      num_common_elements++;
  }
  return prefix + "_" + std::to_string(num_common_elements);
}

bool EmitSeccompCoverageUma(const ProcEntries& proc_entries) {
  size_t total_proc_count = proc_entries.size();
  size_t seccomp_proc_count = 0;
  seccomp_proc_count = std::count_if(
      proc_entries.begin(), proc_entries.end(), [](const ProcEntry& entry) {
        return entry.sandbox_status()[ProcEntry::kSecCompBit] == 1;
      });
  unsigned int seccomp_proc_percentage =
      static_cast<unsigned int>(round((static_cast<float>(seccomp_proc_count) /
                                       static_cast<float>(total_proc_count)) *
                                      100));

  VLOG(1) << "Reporting SecComp coverage UMA metric";
  if (!SendSecCompCoverageToUMA(seccomp_proc_percentage)) {
    LOG(WARNING) << "Could not upload SecComp coverage UMA metric";
    return false;
  }
  return true;
}

bool EmitNnpProcPercentageUma(const ProcEntries& proc_entries) {
  size_t total_proc_count = proc_entries.size();
  size_t nnp_proc_count = 0;
  nnp_proc_count = std::count_if(
      proc_entries.begin(), proc_entries.end(), [](const ProcEntry& entry) {
        return entry.sandbox_status()[ProcEntry::kNoNewPrivsBit] == 1;
      });
  unsigned int nnp_proc_percentage =
      static_cast<unsigned int>(round((static_cast<float>(nnp_proc_count) /
                                       static_cast<float>(total_proc_count)) *
                                      100));

  VLOG(1) << "Reporting no_new_privs process percentage UMA metric";
  if (!SendNnpProcPercentageToUMA(nnp_proc_percentage)) {
    LOG(WARNING)
        << "Could not upload no_new_privs process percentage UMA metric";
    return false;
  }
  return true;
}

bool EmitNonRootProcPercentageUma(const ProcEntries& proc_entries) {
  size_t total_proc_count = proc_entries.size();
  size_t nonroot_proc_count = 0;
  nonroot_proc_count = std::count_if(
      proc_entries.begin(), proc_entries.end(), [](const ProcEntry& entry) {
        return entry.sandbox_status()[ProcEntry::kNonRootBit] == 1;
      });
  unsigned int nonroot_proc_percentage =
      static_cast<unsigned int>(round((static_cast<float>(nonroot_proc_count) /
                                       static_cast<float>(total_proc_count)) *
                                      100));

  VLOG(1) << "Reporting non-root process percentage UMA metric";
  if (!SendNonRootProcPercentageToUMA(nonroot_proc_percentage)) {
    LOG(WARNING) << "Could not upload non-root process percentage UMA metric";
    return false;
  }
  return true;
}

bool EmitUnprivProcPercentageUma(const ProcEntries& proc_entries,
                                 ino_t init_user_ns) {
  size_t total_proc_count = proc_entries.size();
  size_t unpriv_proc_count = 0;
  unpriv_proc_count = std::count_if(
      proc_entries.begin(), proc_entries.end(), [&](const ProcEntry& entry) {
        return entry.sandbox_status()[ProcEntry::kNonRootBit] == 1 &&
               (entry.sandbox_status()[ProcEntry::kNoCapSysAdminBit] == 1 ||
                entry.userns() != init_user_ns);
      });
  unsigned int unpriv_proc_percentage =
      static_cast<unsigned int>(round((static_cast<float>(unpriv_proc_count) /
                                       static_cast<float>(total_proc_count)) *
                                      100));

  VLOG(1) << "Reporting unpriv process percentage UMA metric";
  if (!SendUnprivProcPercentageToUMA(unpriv_proc_percentage)) {
    LOG(WARNING) << "Could not upload unpriv process percentage UMA metric";
    return false;
  }
  return true;
}

bool EmitNonInitNsProcPercentageUma(const ProcEntries& proc_entries,
                                    ino_t init_pid_ns,
                                    ino_t init_mnt_ns) {
  size_t total_proc_count = proc_entries.size();
  size_t non_initns_proc_count = 0;
  non_initns_proc_count = std::count_if(
      proc_entries.begin(), proc_entries.end(), [&](const ProcEntry& entry) {
        return entry.pidns() != init_pid_ns && entry.mntns() != init_mnt_ns;
      });
  unsigned int non_initns_proc_percentage = static_cast<unsigned int>(
      round((static_cast<float>(non_initns_proc_count) /
             static_cast<float>(total_proc_count)) *
            100));

  VLOG(1) << "Reporting non-init namespace process percentage UMA metric";
  if (!SendNonInitNsProcPercentageToUMA(non_initns_proc_percentage)) {
    LOG(WARNING)
        << "Could not upload non-init namespace process percentage UMA metric";
    return false;
  }
  return true;
}

}  // namespace

int Daemon::OnInit() {
  // DBusDaemon::OnInit() initializes the D-Bus connection, making sure |bus_|
  // is populated.
  int ret = brillo::DBusDaemon::OnInit();
  if (ret != EX_OK) {
    return ret;
  }

  // Initializes the audit log reader for accessing the audit log file.
  InitAuditLogReader();

  session_manager_proxy_ = std::make_unique<SessionManagerProxy>(bus_);

  // The raw SessionManagerProxy pointer is un-owned by the SystemContext
  // object.
  system_context_ =
      std::make_unique<SystemContext>(session_manager_proxy_.get());

  return EX_OK;
}

int Daemon::OnEventLoopStarted() {
  ScanForAnomalies();
  ReportUmaMetrics();

  return EX_OK;
}

void Daemon::ScanForAnomalies() {
  VLOG(1) << "Scanning for W+X mounts";
  DoWXMountScan();
  VLOG(1) << "Scanning system processes";
  DoProcScan();
  VLOG(1) << "Scanning for audit log anomalies";
  DoAuditLogScan();

  if (generate_reports_) {
    DoAnomalousSystemReporting();
  }

  brillo::MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&Daemon::ScanForAnomalies, base::Unretained(this)),
      kScanInterval);
}

void Daemon::ReportUmaMetrics() {
  if (!ShouldReport(dev_)) {
    return;
  }

  EmitWXMountCountUma();
  EmitForbiddenIntersectionProcCountUma();
  EmitMemfdExecProcCountUma();
  EmitSandboxingUma();

  brillo::MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&Daemon::ReportUmaMetrics, base::Unretained(this)),
      kUmaReportInterval);
}

void Daemon::DoWXMountScan() {
  all_mounts_ = ReadMounts();
  if (!all_mounts_) {
    LOG(ERROR) << "Failed to read mounts";
    return;
  }

  // Refreshed on every check to have the most up-to-date state.
  system_context_->Refresh();

  for (const auto& e : all_mounts_.value()) {
    if (e.IsWX()) {
      // Have we seen the mount yet?
      if (wx_mounts_.count(e.dest()) == 0) {
        if (e.IsUsbDriveOrArchive()) {
          // Figure out what to log in this case.
          // We could log the fact that the mount exists without logging
          // |src| or |dest|.
          continue;
        }

        if (e.IsNamespaceBindMount() || e.IsKnownMount(*system_context_)) {
          // Namespace mounts happen when a namespace file in /proc/<pid>/ns/
          // gets bind-mounted somewhere else. These mounts can be W+X but are
          // not concerning since they consist of a single file and these files
          // cannot be executed.
          // There are other W+X mounts that are low-risk (e.g. non-persistent
          // mounts) and that we're in the process of fixing. These are
          // considered "known" W+X mounts and are also skipped.
          VLOG(1) << "Not recording W+X mount at '" << e.dest() << "', type "
                  << e.type();
          // In case of a known mount, we need to update the context to remember
          // that this mount was observed, as we might use this information to
          // determine whether it should be ignored again in the future scans.
          system_context_->RecordKnownMountObservation(e.dest());
          continue;
        }

        // We haven't seen the mount, and it's not a type we want to skip, so
        // save it.
        wx_mounts_[e.dest()] = e;
        VLOG(1) << "Found W+X mount at '" << e.dest() << "', type " << e.type();
        VLOG(1) << "|wx_mounts_.size()| = " << wx_mounts_.size();

        // Report metrics on the mount, if not running in dev mode.
        if (ShouldReport(dev_)) {
          // Report /usr/local mounts separately because those can indicate
          // systems where |cros_debug == 0| but the system is still a dev
          // system.
          SecurityAnomaly mount_anomaly =
              e.IsDestInUsrLocal()
                  ? SecurityAnomaly::kMount_InitNs_WxInUsrLocal
                  : SecurityAnomaly::kMount_InitNs_WxNotInUsrLocal;
          if (!SendSecurityAnomalyToUMA(mount_anomaly)) {
            LOG(WARNING) << "Could not upload metrics";
          }
        }
      }
    }
  }
}

void Daemon::DoProcScan() {
  all_procs_ = ReadProcesses(ProcessFilter::kAll);
  if (!all_procs_) {
    return;
  }
  if (!init_proc_) {
    init_proc_ = GetInitProcEntry(all_procs_.value());
  }
  if (!init_proc_) {
    return;
  }

  ProcEntries procs;
  FilterKernelProcesses(all_procs_.value(), procs);

  ProcEntries flagged_procs;
  std::copy_if(procs.begin(), procs.end(), std::back_inserter(flagged_procs),
               [&](const ProcEntry& e) {
                 return IsProcInForbiddenIntersection(e, init_proc_.value());
               });

  forbidden_intersection_procs_ = MaybeProcEntries(flagged_procs);
  if (forbidden_intersection_procs_) {
    VLOG(1) << "|forbidden_intersection_procs_.size()| = "
            << forbidden_intersection_procs_->size();
  }
}

void Daemon::DoAnomalousSystemReporting() {
  // Skip reporting if for all anomaly types either the daemon has previously
  // attempted to send a report or the anomaly does not exist.
  if ((has_attempted_wx_mount_report_ || wx_mounts_.empty()) &&
      (has_attempted_forbidden_intersection_report_ ||
       forbidden_intersection_procs_.value().empty()) &&
      (has_attempted_memfd_exec_report_ ||
       executables_attempting_memfd_exec_.empty())) {
    return;
  }

  // Makes checking for this anomaly type easier.
  ProcEntries anomalous_procs = forbidden_intersection_procs_
                                    ? forbidden_intersection_procs_.value()
                                    : ProcEntries();

  // Stop subsequent reporting attempts for each discovered anomaly type.
  if (!wx_mounts_.empty()) {
    has_attempted_wx_mount_report_ = true;
  }
  if (!anomalous_procs.empty()) {
    has_attempted_forbidden_intersection_report_ = true;
  }
  if (!executables_attempting_memfd_exec_.empty()) {
    has_attempted_memfd_exec_report_ = true;
  }

  if (!ShouldReport(dev_)) {
    VLOG(1) << "Not reporting anomalous system due to dev mode";
    return;
  }
  VLOG(1) << "Attempting to report anomalous system";

  int range = 0;
  int weight = 1;
  // If |dev_| is set or there are memfd execution attempts, always send the
  // report (memfd execution attempts are exceedingly rare so we can afford to
  // upload them all). Otherwise, if W+X anomalies exist, send one in every
  // |CalculateSampleFrequency(wx_mounts_.size())| reports. Finally, if only
  // forbidden intersection violations exist, send one in every
  // |kProcAnomalySampleFrequency| reports.
  if (dev_ || !executables_attempting_memfd_exec_.empty()) {
    range = 1;
  } else if (!wx_mounts_.empty()) {
    range = CalculateSampleFrequency(wx_mounts_.size());
    weight = range;
  } else if (!anomalous_procs.empty() && forbidden_intersection_only_reports_) {
    range = kProcAnomalySampleFrequency;
  }

  // |base::RandInt(min, max)| returns a random int between [min, max], which in
  // this case gives the report one in |range| chance of being sent.
  if (range < 1 || base::RandInt(1, range) > 1) {
    return;
  }

  bool success = ReportAnomalousSystem(wx_mounts_, anomalous_procs,
                                       executables_attempting_memfd_exec_,
                                       all_mounts_, all_procs_, weight, dev_);
  if (!success) {
    // Reporting is best-effort so on failure we just print a warning.
    LOG(WARNING) << "Failed to report anomalous system";
  }

  // Report whether uploading the anomalous system report succeeded.
  if (!SendAnomalyUploadResultToUMA(success)) {
    LOG(WARNING) << "Could not upload metrics";
  }
}

void Daemon::InitAuditLogReader() {
  audit_log_reader_ = std::make_unique<AuditLogReader>(kAuditLogPath);
}

void Daemon::DoAuditLogScan() {
  if (!audit_log_reader_)
    return;

  std::string log_message;
  LogRecord log_record;

  while (audit_log_reader_->GetNextEntry(&log_record)) {
    // This detects a successful memfd_create syscall and reports it to UMA to
    // be used as the baseline metric for memfd execution attempts. The check
    // will not be performed again, once the metric is successfully emitted.
    if (!has_emitted_memfd_baseline_uma_ &&
        log_record.tag == kSyscallRecordTag &&
        secanomalyd::IsMemfdCreate(log_record.message)) {
      // Report baseline condition to UMA if not in dev mode.
      if (ShouldReport(dev_)) {
        if (!SendSecurityAnomalyToUMA(
                SecurityAnomaly::kSuccessfulMemfdCreateSyscall)) {
          LOG(WARNING) << "Could not upload metrics";
        } else {
          has_emitted_memfd_baseline_uma_ = true;
        }
      }
    }
    std::string exe_path;
    if (log_record.tag == kAVCRecordTag &&
        secanomalyd::IsMemfdExecutionAttempt(log_record.message, exe_path)) {
      if (exe_path == secanomalyd::kUnknownExePath) {
        exe_path = GetNextUniquePath(executables_attempting_memfd_exec_,
                                     secanomalyd::kUnknownExePath);
      }
      // Record the anomaly by adding the offending executable path to
      // |executables_attempting_memfd_exec_| set.
      executables_attempting_memfd_exec_.insert(base::FilePath(exe_path));
      VLOG(1) << log_record.message;
      VLOG(1) << "|executables_attempting_memfd_exec_.size()| = "
              << executables_attempting_memfd_exec_.size();
      // Report anomalous condition to UMA if not in dev mode.
      if (ShouldReport(dev_)) {
        if (!SendSecurityAnomalyToUMA(
                SecurityAnomaly::kBlockedMemoryFileExecAttempt))
          LOG(WARNING) << "Could not upload metrics";
      }
    }
  }
}

void Daemon::EmitWXMountCountUma() {
  VLOG(1) << "Reporting W+X mount count UMA metric";
  if (SendWXMountCountToUMA(wx_mounts_.size())) {
    // After successfully reporting W+X mount count, clear the map.
    // If mounts still exist they'll be re-added on the next scan.
    wx_mounts_.clear();
  } else {
    LOG(WARNING) << "Could not upload W+X mount count UMA metric";
  }
}

void Daemon::EmitForbiddenIntersectionProcCountUma() {
  // Skip if already emitted or |forbidden_intersection_procs_| has not yet been
  // populated.
  if (has_emitted_forbidden_intersection_uma_ ||
      !forbidden_intersection_procs_) {
    return;
  }
  // Only report forbidden intersection process count in the logged-in state.
  system_context_->Refresh(/*skip_known_mount_refresh=*/true);
  if (!system_context_->IsUserLoggedIn()) {
    return;
  }
  VLOG(1) << "Reporting forbidden intersection process count UMA metric";
  if (!SendForbiddenIntersectionProcCountToUMA(
          forbidden_intersection_procs_->size())) {
    LOG(WARNING)
        << "Could not upload forbidden intersection process count UMA metric";
  }
}

void Daemon::EmitMemfdExecProcCountUma() {
  VLOG(1) << "Reporting memfd exec process count UMA metric";
  if (SendAttemptedMemfdExecProcCountToUMA(
          executables_attempting_memfd_exec_.size())) {
    // After successfully reporting process count, clear the set. If the same
    // processes attempt memfd executions again, they will be re-added to the
    // set.
    executables_attempting_memfd_exec_.clear();
  } else {
    LOG(WARNING) << "Could not upload memfd exec process count UMA metric";
  }
}

void Daemon::EmitSandboxingUma() {
  if (!has_emitted_landlock_status_uma_) {
    VLOG(1) << "Reporting Landlock status UMA metric";
    // If landlock is in any other state than enabled, such as not supported or
    // an unknown state, we consider it disabled.
    if (!SendLandlockStatusToUMA(system_context_->GetLandlockState() ==
                                 LandlockState::kEnabled)) {
      LOG(WARNING) << "Could not upload Landlock status UMA metric";
    } else {
      has_emitted_landlock_status_uma_ = true;
    }
  }

  // Refresh the login state.
  system_context_->Refresh(/*skip_known_mount_refresh=*/true);

  if ((!has_emitted_seccomp_coverage_uma_ ||
       !has_emitted_nonroot_proc_percentage_uma_ ||
       !has_emitted_unpriv_proc_percentage_uma_) &&
      system_context_->IsUserLoggedIn()) {
    MaybeProcEntries maybe_proc_entries =
        ReadProcesses(ProcessFilter::kNoKernelTasks);
    if (!maybe_proc_entries.has_value() ||
        maybe_proc_entries.value().size() == 0) {
      return;
    }

    if (!has_emitted_seccomp_coverage_uma_) {
      has_emitted_seccomp_coverage_uma_ =
          EmitSeccompCoverageUma(maybe_proc_entries.value());
    }

    if (!has_emitted_nnp_proc_percentage_uma_) {
      has_emitted_nnp_proc_percentage_uma_ =
          EmitNnpProcPercentageUma(maybe_proc_entries.value());
    }

    if (!has_emitted_nonroot_proc_percentage_uma_) {
      has_emitted_nonroot_proc_percentage_uma_ =
          EmitNonRootProcPercentageUma(maybe_proc_entries.value());
    }

    // For the rest of the metrics, we need to have the init process entry.
    if (!init_proc_) {
      return;
    }

    if (!has_emitted_unpriv_proc_percentage_uma_) {
      has_emitted_unpriv_proc_percentage_uma_ = EmitUnprivProcPercentageUma(
          maybe_proc_entries.value(), init_proc_.value().userns());
    }

    if (!has_emitted_non_initns_proc_percentage_uma_) {
      has_emitted_non_initns_proc_percentage_uma_ =
          EmitNonInitNsProcPercentageUma(maybe_proc_entries.value(),
                                         init_proc_.value().pidns(),
                                         init_proc_.value().mntns());
    }
  }
}

}  // namespace secanomalyd
