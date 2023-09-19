// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECANOMALYD_REPORTER_H_
#define SECANOMALYD_REPORTER_H_

#include <optional>
#include <string>

#include "secanomalyd/mount_entry.h"
#include "secanomalyd/mounts.h"
#include "secanomalyd/processes.h"

namespace secanomalyd {

using MaybeReport = std::optional<std::string>;

bool ShouldReport(bool report_in_dev_mode);

// Exposed mostly for testing.
std::string GenerateMountSignature(const MountEntryMap& wx_mounts);
std::optional<std::string> GenerateProcSignature(const ProcEntries& procs);
std::optional<std::string> GeneratePathSignature(const FilePaths& paths);

// Exposed mostly for testing.
MaybeReport GenerateAnomalousSystemReport(
    const MountEntryMap& wx_mounts,
    const ProcEntries& forbidden_intersection_procs,
    const FilePaths& executables_attempting_memfd_exec,
    const MaybeMountEntries& all_mounts,
    const MaybeProcEntries& all_procs);

bool SendReport(base::StringPiece report,
                brillo::Process* crash_reporter,
                int weight,
                bool report_in_dev_mode);

bool ReportAnomalousSystem(const MountEntryMap& wx_mounts,
                           const ProcEntries& forbidden_intersection_procs,
                           const FilePaths& executables_attempting_memfd_exec,
                           const MaybeMountEntries& all_mounts,
                           const MaybeProcEntries& all_procs,
                           int weight,
                           bool report_in_dev_mode);

}  // namespace secanomalyd

#endif  // SECANOMALYD_REPORTER_H_
