// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>

#include "init/libpreservation/file_preseeder.h"
#include "init/libpreservation/preservation.h"

int main(int argc, char** argv) {
  std::string partition_layout;
  base::FilePath rootdir("/");
  base::FilePath stateful_mount = rootdir.Append("mnt/stateful_partition");
  base::FilePath metadata_mount =
      rootdir.Append("mnt/chromeos_metadata_partition");
  base::FilePath system_encryption_key = metadata_mount.Append("encrypted.key");
  base::FilePath preseeding_data = metadata_mount.Append("preseeder.proto");

  // Check if the device is using the default key layout.
  if (!base::PathExists(system_encryption_key)) {
    return EXIT_SUCCESS;
  }

  libpreservation::FilePreseeder preseeder({base::FilePath("unencrypted")},
                                           rootdir, stateful_mount,
                                           preseeding_data);

  std::set<base::FilePath> file_allowlist;
  for (auto path : libpreservation::GetPreservationFileList()) {
    file_allowlist.insert(base::FilePath(path));
  }
  for (auto path :
       libpreservation::GetFactoryPreservationPathList(stateful_mount)) {
    file_allowlist.insert(base::FilePath(path));
  }
  for (auto path : libpreservation::GetStartupPreseedingPaths()) {
    file_allowlist.insert(base::FilePath(path));
  }

  if (!preseeder.SaveFileState(file_allowlist)) {
    return EXIT_FAILURE;
  }

  sync();

  return EXIT_SUCCESS;
}
