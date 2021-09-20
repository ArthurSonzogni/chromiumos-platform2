// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/sys_byteorder.h>

#include <hps/hps_reg.h>
#include <hps/utils.h>

namespace hps {

bool ReadVersionFromFile(const base::FilePath& mcu, uint32_t* version) {
  uint32_t version_tmp;
  base::File file(mcu,
                  base::File::Flags::FLAG_OPEN | base::File::Flags::FLAG_READ);
  if (!file.IsValid()) {
    LOG(ERROR) << "ReadVersionFromFile: \"" << mcu
               << "\": " << base::File::ErrorToString(file.error_details());
    return false;
  }
  int read = file.Read(kVersionOffset, reinterpret_cast<char*>(&version_tmp),
                       sizeof(version_tmp));
  if (read < 0) {
    LOG(ERROR) << "ReadVersionFromFile: \"" << mcu
               << "\": " << base::File::ErrorToString(file.GetLastFileError());
    return false;
  }
  if (sizeof(version_tmp) != read) {
    LOG(ERROR) << "ReadVersionFromFile: \"" << mcu << "\": short read";
    return false;
  }
  *version = base::NetToHost32(version_tmp);
  return true;
}

const char* HpsRegToString(const HpsReg reg) {
  switch (reg) {
    case HpsReg::kMagic:
      return "kMagic";
    case HpsReg::kHwRev:
      return "kHwRev";
    case HpsReg::kSysStatus:
      return "kSysStatus";
    case HpsReg::kSysCmd:
      return "kSysCmd";
    case HpsReg::kApplVers:
      return "kApplVers";
    case HpsReg::kBankReady:
      return "kBankReady";
    case HpsReg::kError:
      return "kError";
    case HpsReg::kFeatEn:
      return "kFeatEn";
    case HpsReg::kF1:
      return "kF1";
    case HpsReg::kF2:
      return "kF2";
    case HpsReg::kFirmwareVersionHigh:
      return "kFirmwareVersionHigh";
    case HpsReg::kFirmwareVersionLow:
      return "kFirmwareVersionLow";

    case HpsReg::kMax:
      return "kMax";
  }
  return "unknown";
}

}  // namespace hps
