// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secanomalyd/daemon.h"

#include <sysexits.h>

#include <memory>
#include <string>
#include <vector>

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

#include "secanomalyd/metrics.h"
#include "secanomalyd/mount_entry.h"
#include "secanomalyd/mounts.h"
#include "secanomalyd/reporter.h"
#include "secanomalyd/system_context.h"

namespace secanomalyd {

namespace {

constexpr int kSampleFrequency = 10;

constexpr base::TimeDelta kCheckInterval = base::Seconds(30);
// Per Platform.DailyUseTime histogram this interval should ensure that enough
// users run the reporting.
constexpr base::TimeDelta kReportWXMountCountInterval = base::Hours(2);

}  // namespace

int Daemon::OnInit() {
  // DBusDaemon::OnInit() initializes the D-Bus connection, making sure |bus_|
  // is populated.
  int ret = brillo::DBusDaemon::OnInit();
  if (ret != EX_OK) {
    return ret;
  }

  session_manager_proxy_ = std::make_unique<SessionManagerProxy>(bus_);

  return EX_OK;
}

int Daemon::OnEventLoopStarted() {
  CheckWXMounts();
  ReportWXMountCount();

  return EX_OK;
}

void Daemon::CheckWXMounts() {
  VLOG(1) << "Checking for W+X mounts";

  DoWXMountCheck();

  brillo::MessageLoop::current()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&Daemon::CheckWXMounts, base::Unretained(this)),
      kCheckInterval);
}

void Daemon::ReportWXMountCount() {
  VLOG(1) << "Reporting W+X mount count";

  DoWXMountCountReporting();

  brillo::MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&Daemon::ReportWXMountCount, base::Unretained(this)),
      kReportWXMountCountInterval);
}

void Daemon::DoWXMountCheck() {
  MaybeMountEntries mount_entries = ReadMounts(MountFilter::kAll);
  if (!mount_entries) {
    LOG(ERROR) << "Failed to read mounts";
    return;
  }

  // Recreated on every check to have the most up-to-date state.
  // The raw SessionManagerProxy pointer is un-owned by the SystemContext
  // object.
  SystemContext context(session_manager_proxy_.get());

  for (const auto& e : mount_entries.value()) {
    if (e.IsWX()) {
      // Have we seen the mount yet?
      if (wx_mounts_.count(e.dest()) == 0) {
        if (e.IsUsbDriveOrArchive()) {
          // Figure out what to log in this case.
          // We could log the fact that the mount exists without logging
          // |src| or |dest|.
          continue;
        }

        if (e.IsNamespaceBindMount() || e.IsKnownMount(context)) {
          // Namespace mounts happen when a namespace file in /proc/<pid>/ns/
          // gets bind-mounted somewhere else. These mounts can be W+X but are
          // not concerning since they consist of a single file and these files
          // cannot be executed.
          // There are other W+X mounts that are low-risk (e.g. only exist on
          // the login screen) and that we're in the process of fixing. These
          // are considered "known" W+X mounts and are also skipped.
          VLOG(1) << "Not recording W+X mount at '" << e.dest() << "', type "
                  << e.type();
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

void Daemon::DoWXMountCountReporting() {
  if (ShouldReport(dev_)) {
    if (!SendWXMountCountToUMA(wx_mounts_.size())) {
      LOG(WARNING) << "Could not upload W+X mount count";
    }

    // Should we send an anomalous system report?
    if (generate_reports_ && !has_attempted_report_ && wx_mounts_.size() > 0) {
      // Stop subsequent reporting attempts for this execution.
      has_attempted_report_ = true;
      // Send one out of every |kSampleFrequency| reports, unless |dev_| is set.
      // |base::RandInt()| returns a random int in [min, max].
      int range = dev_ ? 1 : kSampleFrequency;
      if (base::RandInt(1, range) > 1) {
        return;
      }

      bool success = ReportAnomalousSystem(wx_mounts_, range, dev_);
      if (!success) {
        // Reporting is best-effort so on failure we just print a warning.
        LOG(WARNING) << "Failed to report anomalous system";
      }

      // Report whether uploading the anomalous system report succeeded.
      if (!SendAnomalyUploadResultToUMA(success)) {
        LOG(WARNING) << "Could not upload metrics";
      }
    }
  }
}

}  // namespace secanomalyd
