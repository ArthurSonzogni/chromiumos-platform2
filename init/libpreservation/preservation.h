// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_LIBPRESERVATION_PRESERVATION_H_
#define INIT_LIBPRESERVATION_PRESERVATION_H_

#include <set>
#include <string>

#include <base/files/file_path.h>
#include <brillo/brillo_export.h>

namespace libpreservation {

// Generates a list of

// Generates a list of files that needs to be preserved across powerwash
// and on default_key_stateful setup on first boot.
BRILLO_EXPORT std::set<std::string> GetPreservationFileList(
    bool safe_wipe,
    bool ad_migration_wipe,
    bool rollback_wipe,
    bool rma_wipe,
    bool debug_build,
    bool dev_mode);

BRILLO_EXPORT std::set<std::string> GetPreservationFileList();

BRILLO_EXPORT std::set<std::string> GetStartupPreseedingPaths();

BRILLO_EXPORT std::set<std::string> GetFactoryPreservationPathList(
    const base::FilePath& mount_path);

}  // namespace libpreservation

#endif  // INIT_LIBPRESERVATION_PRESERVATION_H_
