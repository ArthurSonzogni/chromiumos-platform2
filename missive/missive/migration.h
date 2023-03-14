// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Migration from /var/cache/reporting to /var/spool/reporting.
// Design doc: go/missive-move

#ifndef MISSIVE_MISSIVE_MIGRATION_H_
#define MISSIVE_MISSIVE_MIGRATION_H_

#include <base/files/file_path.h>

#include "missive/util/status.h"

namespace reporting {
// Migrates from the old reporting directory to the new one. Returns the status.
// In production code, this should be called as
//   Migration(base::FilePath("/var/cache/reporting"),
//   base::FilePath("/var/spool/reporting"))
Status Migrate(const base::FilePath& src, const base::FilePath& dest);
}  // namespace reporting

#endif  // MISSIVE_MISSIVE_MIGRATION_H_
