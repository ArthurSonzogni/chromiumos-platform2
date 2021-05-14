// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secanomalyd/daemon.h"

#include <string>
#include <vector>

#include <base/command_line.h>
#include <base/files/file_util.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_piece.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>

#include <brillo/process/process.h>
#include <brillo/message_loops/message_loop.h>

#include <vboot/crossystem.h>

#include "secanomalyd/metrics.h"
#include "secanomalyd/mount_entry.h"

namespace {

constexpr base::TimeDelta kCheckInterval = base::TimeDelta::FromSeconds(30);

constexpr char kProcSelfMountsPath[] = "/proc/self/mounts";

constexpr char kCrashReporterPath[] = "/sbin/crash_reporter";
constexpr char kSecurityAnomalyFlag[] = "--security_anomaly";

bool ShouldReport(bool report_in_dev_mode) {
  // Reporting should only happen when booted in Verified mode and not running
  // a developer image, unless explicitly instructed otherwise.
  return ::VbGetSystemPropertyInt("cros_debug") == 0 || report_in_dev_mode;
}

bool ReportMount(const MountEntry& e, bool report_in_dev_mode) {
  if (!ShouldReport(report_in_dev_mode)) {
    VLOG(1) << "Not in Verified mode, not reporting " << e.dest();
    return true;
  }

  VLOG(1) << "secanomalyd invoking crash_reporter";

  brillo::ProcessImpl crash_reporter;
  crash_reporter.AddArg(kCrashReporterPath);
  crash_reporter.AddArg(kSecurityAnomalyFlag);

  crash_reporter.RedirectUsingPipe(STDIN_FILENO, true);
  CHECK(crash_reporter.Start());
  int stdin_fd = crash_reporter.GetPipe(STDIN_FILENO);
  CHECK(base::WriteFileDescriptor(stdin_fd, e.src().value().data(),
                                  e.src().value().length()));
  std::string newline = "\n";
  CHECK(base::WriteFileDescriptor(stdin_fd, newline.data(), newline.length()));
  CHECK(base::WriteFileDescriptor(stdin_fd, e.dest().value().data(),
                                  e.dest().value().length()));
  CHECK_GE(IGNORE_EINTR(close(stdin_fd)), 0);
  CHECK_EQ(0, crash_reporter.Wait());

  return true;
}

}  // namespace

int Daemon::OnEventLoopStarted() {
  CheckRwMounts();

  return 0;
}

void Daemon::CheckRwMounts() {
  VLOG(1) << "Checking for R/W mounts";

  DoRwMountCheck();

  brillo::MessageLoop::current()->PostDelayedTask(
      FROM_HERE, base::Bind(&Daemon::CheckRwMounts, base::Unretained(this)),
      kCheckInterval);
}

void Daemon::DoRwMountCheck() {
  std::string proc_mounts;
  if (!base::ReadFileToStringNonBlocking(base::FilePath(kProcSelfMountsPath),
                                         &proc_mounts)) {
    PLOG(ERROR) << "Failed to read " << kProcSelfMountsPath;
    return;
  }

  std::vector<base::StringPiece> mounts = base::SplitStringPiece(
      proc_mounts, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  for (const auto& m : mounts) {
    MountEntry e(m);
    if (e.IsWX()) {
      // Have we seen the mount yet?
      if (wx_mounts_.count(e.dest()) == 0) {
        if (e.IsUsbDriveOrArchive()) {
          // Figure out what to log in this case.
          // We could log the fact that the mount exists without logging
          // |src| or |dest|.
          continue;
        }
        // If we haven't seen the mount, save it.
        wx_mounts_[e.dest()] = e;
        VLOG(1) << "Found W+X mount at '" << e.dest() << "', type " << e.type();

        if (e.type() == "nsfs" || e.type() == "proc") {
          // "nsfs" mounts happen when a namespace file in /proc/<pid>/ns/ gets
          // bind-mounted somewhere else. These mounts can be W+X but are not
          // concerning since they are single files and these files cannot be
          // executed.
          // On 3.18 kernels these mounts show up as type "proc" rather than
          // type "nsfs".
          // TODO(crbug.com/1204604): Remove the "proc" exception after 3.18
          // kernels go away.
          VLOG(1) << "Not reporting '" << e.dest() << "', mount type "
                  << e.type();
          continue;
        }

        // Report metrics on it, if not running in dev mode.
        if (ShouldReport(dev_)) {
          if (!SendSecurityAnomalyToUMA(SecurityAnomaly::kMountInitNsWx)) {
            LOG(WARNING) << "Could not upload metrics";
          }
        }

        // And report the actual anomalous mount, when required to.
        if (generate_reports_) {
          ReportMount(e, dev_);
        }
      }
    }
  }
}
