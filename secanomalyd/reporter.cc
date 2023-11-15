// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secanomalyd/reporter.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/numerics/safe_conversions.h>
#include <base/rand_util.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/process/process.h>
#include <crypto/sha2.h>
#include <vboot/crossystem.h>

#include "secanomalyd/mount_entry.h"
#include "secanomalyd/mounts.h"
#include "secanomalyd/processes.h"

namespace secanomalyd {

namespace {

constexpr size_t kHashPrefixLengthInBytes = 5u;

constexpr char kRootPathReplacement[] = "slashroot";
constexpr char kCrashReporterPath[] = "/sbin/crash_reporter";
constexpr char kSecurityAnomalyFlag[] = "--security_anomaly";
constexpr char kWeightFlag[] = "--weight";

}  // namespace

bool ShouldReport(bool report_in_dev_mode) {
  // Reporting should only happen when booted in Verified mode and not running
  // a developer image, unless explicitly instructed otherwise.
  return ::VbGetSystemPropertyInt("cros_debug") == 0 || report_in_dev_mode;
}

std::string GenerateMountSignature(const MountEntryMap& wx_mounts) {
  std::vector<std::string> dests;

  for (const auto& p : wx_mounts) {
    dests.emplace_back(p.first.value());
  }

  std::string signature;
  // Use the first path as a visible sentinel for the signature.
  // If the anomalous mount is on '/', replace the destination path with a
  // default value so that the signature doesn't have consecutive dashes.
  if (dests[0] != "/") {
    base::ReplaceChars(dests[0], "/", "-", &signature);
  } else {
    signature = kRootPathReplacement;
  }

  // Hash the string resulting from joining all mount destinations separated
  // by newlines. Take the first five bytes and use that to complete the
  // signature.
  std::vector<uint8_t> prefix(kHashPrefixLengthInBytes);
  crypto::SHA256HashString(base::JoinString(dests, "\n"), prefix.data(),
                           prefix.size());
  base::StrAppend(&signature, {"-", base::HexEncode(prefix)});

  return signature;
}

std::optional<std::string> GenerateProcSignature(const ProcEntries& procs) {
  if (procs.empty()) {
    return std::nullopt;
  }
  std::string signature;
  int signature_proc = base::RandInt(0, procs.size() - 1);
  signature = procs[base::checked_cast<size_t>(signature_proc)].comm();
  return std::optional<std::string>(signature);
}

std::optional<std::string> GeneratePathSignature(const FilePaths& paths) {
  if (paths.empty()) {
    return std::nullopt;
  }
  base::FilePath signature_path = *paths.begin();
  return std::optional<std::string>(signature_path.value());
}

MaybeReport GenerateAnomalousSystemReport(
    const MountEntryMap& wx_mounts,
    const ProcEntries& forbidden_intersection_procs,
    const FilePaths& executables_attempting_memfd_exec,
    const MaybeMountEntries& all_mounts,
    const MaybeProcEntries& all_procs) {
  // First line: signature
  // Second line: metadata
  //    signals: wx-mount | forbidden-intersection-violation |
  //      memfd-exec-attempt | multiple-anomalies
  //    dest: /usr/local, e.g.
  // Third+ line: content
  std::vector<std::string> lines;

  // Generate signature based on the anomaly type. If multiple anomaly types are
  // present, the order of preference for signature generation is memfd exec
  // attempt, then W+X mount, then forbidden intersection process. At least one
  // anomaly has to be preset to proceed.
  std::optional<std::string> signature;
  if (!executables_attempting_memfd_exec.empty()) {
    signature = GeneratePathSignature(executables_attempting_memfd_exec);
  } else if (!wx_mounts.empty()) {
    signature = GenerateMountSignature(wx_mounts);
  } else if (!forbidden_intersection_procs.empty()) {
    signature = GenerateProcSignature(forbidden_intersection_procs);
  } else {
    return std::nullopt;
  }
  if (!signature) {
    return std::nullopt;
  }
  lines.emplace_back(signature.value());

  // Generate metadata.
  // Metadata are a set of key-value pairs where keys and values are separated
  // by \x01 and pairs are separated by \x02:
  // 'signals\x01wx-mount\x02dest\x01/usr/local'
  //
  // Signal which anomaly type triggered the report generation, or whether
  // the report was generated due to multiple anomalies.
  std::string metadata = "signals\x01";
  if (wx_mounts.empty() && executables_attempting_memfd_exec.empty()) {
    base::StrAppend(&metadata, {"forbidden-intersection-violation"});
  } else if (executables_attempting_memfd_exec.empty() &&
             forbidden_intersection_procs.empty()) {
    base::StrAppend(&metadata, {"wx-mount"});
  } else if (forbidden_intersection_procs.empty() && wx_mounts.empty()) {
    base::StrAppend(&metadata, {"memfd-exec-attempt"});
  } else {
    base::StrAppend(&metadata, {"multiple-anomalies"});
  }
  // Indicate the specific anomaly used for the signature generation.
  base::StrAppend(&metadata, {"\x02"});
  if (!executables_attempting_memfd_exec.empty()) {
    base::StrAppend(&metadata, {"executable", "\x01", signature.value()});
  } else if (!wx_mounts.empty()) {
    base::FilePath dest = wx_mounts.begin()->first;
    base::StrAppend(&metadata, {"dest"
                                "\x01",
                                dest.value()});
  } else if (!forbidden_intersection_procs.empty()) {
    base::StrAppend(&metadata, {"comm", "\x01", signature.value()});
  }
  lines.emplace_back(metadata);

  // List anomalous conditions.
  lines.emplace_back("=== Anomalous conditions ===");
  if (!wx_mounts.empty()) {
    lines.emplace_back("=== W+X mounts ===");
    for (const auto& tuple : wx_mounts) {
      lines.push_back(tuple.second.FullDescription());
    }
  }
  if (!forbidden_intersection_procs.empty()) {
    lines.emplace_back("=== Forbidden intersection processes ===");
    for (const auto& e : forbidden_intersection_procs) {
      lines.push_back(e.comm() + " " + e.args());
    }
  }
  if (!executables_attempting_memfd_exec.empty()) {
    lines.emplace_back("=== Executables attempting memfd exec ===");
    for (const auto& e : executables_attempting_memfd_exec) {
      lines.push_back(e.value());
    }
  }

  // Include the list of all mounts.
  lines.emplace_back("=== All mounts ===");
  if (all_mounts) {
    // List mounts.
    for (const auto& mount_entry : all_mounts.value()) {
      lines.push_back(mount_entry.FullDescription());
    }
  } else {
    lines.emplace_back("Could not obtain mounts");
  }

  // Include the list of all processes.
  lines.emplace_back("=== All processes ===");
  if (all_procs) {
    // List processes.
    for (const auto& proc_entry : all_procs.value()) {
      lines.emplace_back(proc_entry.args());
    }
  } else {
    lines.emplace_back("Could not obtain processes");
  }

  // Ensure reports have a trailing newline. Trailing newlines make reports
  // easier to read in a terminal.
  lines.emplace_back("");
  return MaybeReport(base::JoinString(lines, "\n"));
}

bool SendReport(std::string_view report,
                brillo::Process* crash_reporter,
                int weight,
                bool report_in_dev_mode) {
  if (!ShouldReport(report_in_dev_mode)) {
    VLOG(1) << "Not in Verified mode, not reporting";
    return true;
  }

  VLOG(1) << "secanomalyd invoking crash_reporter";

  crash_reporter->AddArg(kCrashReporterPath);
  crash_reporter->AddArg(kSecurityAnomalyFlag);
  crash_reporter->AddArg(base::StringPrintf("%s=%d", kWeightFlag, weight));

  crash_reporter->RedirectUsingPipe(STDIN_FILENO, true /*is_input*/);

  if (!crash_reporter->Start()) {
    LOG(ERROR) << "Failed to start crash reporting process";
    return false;
  }

  int stdin_fd = crash_reporter->GetPipe(STDIN_FILENO);
  if (stdin_fd < 0) {
    LOG(ERROR) << "Failed to get stdin pipe for crash reporting process";
    return false;
  }

  {
    base::ScopedFD stdin(stdin_fd);

    if (!base::WriteFileDescriptor(stdin_fd, report)) {
      LOG(ERROR) << "Failed to write report to crash reporting process' stdin";
      return false;
    }
  }

  // |crash_reporter| returns 0 on success.
  return crash_reporter->Wait() == 0;
}

bool ReportAnomalousSystem(const MountEntryMap& wx_mounts,
                           const ProcEntries& forbidden_intersection_procs,
                           const FilePaths& executables_attempting_memfd_exec,
                           const MaybeMountEntries& all_mounts,
                           const MaybeProcEntries& all_procs,
                           int weight,
                           bool report_in_dev_mode) {
  // Filter out private mounts before upload.
  MaybeMountEntries uploadable_mounts = FilterPrivateMounts(all_mounts);

  // Filter out processes not in the init PID namespace.
  ProcEntries init_pidns_procs;
  if (all_procs) {
    FilterNonInitPidNsProcesses(all_procs.value(), init_pidns_procs);
  }

  MaybeReport anomaly_report = GenerateAnomalousSystemReport(
      wx_mounts, forbidden_intersection_procs,
      executables_attempting_memfd_exec, uploadable_mounts,
      MaybeProcEntries(init_pidns_procs));

  if (!anomaly_report) {
    LOG(ERROR) << "Failed to generate anomalous system report";
    return false;
  }

  brillo::ProcessImpl crash_reporter;
  if (!SendReport(anomaly_report.value(), &crash_reporter, weight,
                  report_in_dev_mode)) {
    LOG(ERROR) << "Failed to send anomalous system report";
    return false;
  }

  return true;
}

}  // namespace secanomalyd
