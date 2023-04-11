// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EVENTS_CRASH_EVENTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_EVENTS_CRASH_EVENTS_H_

#include <cstdint>
#include <vector>

#include <base/strings/string_piece.h>
#include <base/time/time.h>

#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {

// Parses log string as the same format used in /var/log/chrome/Crash
// Reports/uploads.log and returns the result. Performs a functionality similar
// to `TextLogUploadList::TryParseJsonLogEntry` in Chromium. If there are any
// invalid log entries, they would be logged and all of the rest (i.e., the
// valid log entries) are returned.
//
// Params:
//   - log: The content of the the log string to be parsed.
//   - is_uploaded: Whether the log is taken from uploads.log.
//   - creation_time: The creation time of uploads.log. Used only when
//     is_uploaded is true.
//   - init_offset: The initial offset of the log string in uploads.log. Used
//     only when is_uploaded is true.
std::vector<ash::cros_healthd::mojom::CrashEventInfoPtr> ParseUploadsLog(
    base::StringPiece log,
    bool is_uploaded,
    base::Time creation_time,
    uint64_t init_offset);
}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EVENTS_CRASH_EVENTS_H_
