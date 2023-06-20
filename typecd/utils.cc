// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/utils.h"

#include <iomanip>
#include <sstream>
#include <string>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

namespace typecd {

bool ReadHexFromPath(const base::FilePath& path, uint32_t* val) {
  std::string val_str;
  if (!base::ReadFileToString(path, &val_str)) {
    LOG(ERROR) << "Couldn't read value from path " << path;
    return false;
  }
  base::TrimWhitespaceASCII(val_str, base::TRIM_TRAILING, &val_str);

  if (!base::HexStringToUInt(val_str.c_str(), val)) {
    LOG(ERROR) << "Error parsing hex value: " << val_str;
    return false;
  }

  return true;
}

std::string FormatHexString(uint32_t val, int width) {
  std::stringstream out;
  out << std::hex << std::setfill('0') << std::setw(width) << val;
  return out.str();
}

}  // namespace typecd
