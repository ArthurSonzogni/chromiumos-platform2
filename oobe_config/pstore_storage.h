// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef OOBE_CONFIG_PSTORE_STORAGE_H_
#define OOBE_CONFIG_PSTORE_STORAGE_H_

#include <string>

#include <base/files/file_path.h>
#include <base/optional.h>

namespace oobe_config {

// These functions take advantage of a utility called pstore: Writes to /dev/
// pmsg0 are persisted in /sys/fs/pstore/pmsg-ramoops-[ID] across exactly one
// reboot.

// Prepares data to be stored in pstore across rollback by formatting and
// staging in a special file to be picked up by clobber. Returns whether
// staging was successful.
// Note that clobber_state does the actual appending to pstore right before
// wiping the device.
bool StageForPstore(const std::string& data, const base::FilePath& root_path);

// Loads data directly from pstore. Returns `base::nullopt` if
// no rollback data was found.
base::Optional<std::string> LoadFromPstore(const base::FilePath& root_path);

}  // namespace oobe_config

#endif  // OOBE_CONFIG_PSTORE_STORAGE_H_
