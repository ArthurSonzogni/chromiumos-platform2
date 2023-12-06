// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_id/utils.h"

#include <base/files/file_util.h>
#include <base/strings/string_util.h>

namespace flex_id {

std::optional<std::string> ReadAndTrimFile(const base::FilePath& file_path) {
  std::string out;
  if (!base::ReadFileToString(file_path, &out))
    return std::nullopt;

  base::TrimWhitespaceASCII(out, base::TRIM_ALL, &out);

  return out;
}

}  // namespace flex_id
