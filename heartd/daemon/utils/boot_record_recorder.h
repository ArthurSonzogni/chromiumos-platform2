// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEARTD_DAEMON_UTILS_BOOT_RECORD_RECORDER_H_
#define HEARTD_DAEMON_UTILS_BOOT_RECORD_RECORDER_H_

#include <base/files/file_path.h>

#include "heartd/daemon/database.h"

namespace heartd {

constexpr char kMetricsPath[] = "var/log/metrics/";
constexpr char kBootIDPath[] = "var/log/boot_id.log";

void RecordBootMetrics(const base::FilePath& root_dir, const Database* db_ptr);

}  // namespace heartd

#endif  // HEARTD_DAEMON_UTILS_BOOT_RECORD_RECORDER_H_
