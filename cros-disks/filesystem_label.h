// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_FILESYSTEM_LABEL_H_
#define CROS_DISKS_FILESYSTEM_LABEL_H_

#include <string>
#include <string_view>

namespace cros_disks {

enum class LabelError {
  kSuccess = 0,
  kUnsupportedFilesystem = 1,
  kLongName = 2,
  kInvalidCharacter = 3,
};

// Returns true if the file system type is supported, and |volume_label|
// contains only allowed characters and length is not greater than the file
// system's limit.
LabelError ValidateVolumeLabel(const std::string& volume_label,
                               const std::string& filesystem_type);

// Returns a sanitized name, which can then be used as a directory name. The
// returned string is not empty, it does not start with a dot and it does not
// contain any slash.
std::string Sanitize(std::string_view name);

}  // namespace cros_disks

#endif  // CROS_DISKS_FILESYSTEM_LABEL_H_
