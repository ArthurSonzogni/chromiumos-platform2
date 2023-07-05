// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootsplash/utils.h"

#include <string>

#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <re2/re2.h>

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

int GetMaxBootSplashFrameNumber(bool feature_simon_enabled) {
  int frame_number = -1;
  base::FilePath boot_splash_path =
      paths::GetBootSplashAssetsDir(feature_simon_enabled);

  base::FileEnumerator dir_enum(boot_splash_path, false /* recursive */,
                                base::FileEnumerator::FILES);
  for (base::FilePath boot_splash_frame_name = dir_enum.Next();
       !boot_splash_frame_name.empty();
       boot_splash_frame_name = dir_enum.Next()) {
    std::string regex_str = base::StringPrintf(
        "%s(\\d+)%s", paths::kBootSplashFilenamePrefix, paths::kImageExtension);
    const std::string filename = boot_splash_frame_name.BaseName().value();
    int curr_frame_num = -1;

    if (RE2::FullMatch(filename, regex_str, &curr_frame_num)) {
      if (curr_frame_num > frame_number) {
        frame_number = curr_frame_num;
      }
    }
  }

  return frame_number;
}

}  // namespace utils
