// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECANOMALYD_REPORTER_H_
#define SECANOMALYD_REPORTER_H_

#include <string>

#include <base/optional.h>

#include "secanomalyd/mount_entry.h"
#include "secanomalyd/mounts.h"
#include "secanomalyd/processes.h"

namespace secanomalyd {

using MaybeReport = base::Optional<std::string>;

bool ShouldReport(bool report_in_dev_mode);

// Exposed mostly for testing.
std::string GenerateSignature(const MountEntryMap& wx_mounts);

// Exposed mostly for testing.
MaybeReport GenerateAnomalousSystemReport(const MountEntryMap& wx_mounts,
                                          const MaybeMountEntries& all_mounts,
                                          const MaybeProcEntries& all_procs);

bool SendReport(base::StringPiece report,
                brillo::Process* crash_reporter,
                int weight,
                bool report_in_dev_mode);

bool ReportAnomalousSystem(const MountEntryMap& wx_mounts,
                           int weight,
                           bool report_in_dev_mode);

}  // namespace secanomalyd

#endif  // SECANOMALYD_REPORTER_H_
