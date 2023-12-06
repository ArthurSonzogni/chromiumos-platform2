// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLEX_ID_UTILS_H_
#define FLEX_ID_UTILS_H_

#include <optional>
#include <string>

#include <base/files/file_path.h>

namespace flex_id {

// Reads the given file contents to a string and trims all whitespace.
std::optional<std::string> ReadAndTrimFile(const base::FilePath& file_path);

}  // namespace flex_id

#endif  // FLEX_ID_UTILS_H_
