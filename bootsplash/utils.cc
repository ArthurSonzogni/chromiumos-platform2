// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootsplash/utils.h"

#include <string>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>

#include "bootsplash/paths.h"

namespace utils {

bool IsHiResDisplay() {
  uint32_t is_hi_res = 0;
  std::string hi_res;
  base::FilePath hi_res_path = paths::Get(paths::kFreconHiRes);

  if (!base::ReadFileToString(hi_res_path, &hi_res)) {
    LOG(ERROR) << "Failed to read: '" << hi_res_path.value()
               << "'. Defaulting to low resolution.";
    return false;
  }

  if (!base::StringToUint(hi_res, &is_hi_res)) {
    LOG(ERROR) << "Failed to parse: '" << hi_res
               << "'. Defaulting to low resolution.";
    return false;
  }

  return is_hi_res;
}

}  // namespace utils
