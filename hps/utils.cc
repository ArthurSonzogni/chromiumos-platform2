// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_util.h>
#include <base/sys_byteorder.h>

#include <hps/hps_reg.h>
#include <hps/utils.h>

namespace hps {

#define ENUM_BIT(e) BIT(static_cast<int>(e))

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
    case HpsReg::kFeature0:
      return "kFeature0";
    case HpsReg::kFeature1:
      return "kFeature1";
    case HpsReg::kFirmwareVersionHigh:
      return "kFirmwareVersionHigh";
    case HpsReg::kFirmwareVersionLow:
      return "kFirmwareVersionLow";

    case HpsReg::kMax:
      return "kMax";
  }
  return "unknown";
}

std::string HpsRegValToString(HpsReg reg, uint16_t val) {
  std::vector<std::string> ret;
  switch (reg) {
    case HpsReg::kSysStatus:
      if (val & kOK) {
        ret.push_back("kOK");
        val ^= kOK;
      }
      if (val & kFault) {
        ret.push_back("kFault");
        val ^= kFault;
      }
      if (val & kStage1Verified) {
        ret.push_back("kStage1Verified");
        val ^= kStage1Verified;
      }
      if (val & kStage1NotVerified) {
        ret.push_back("kStage1NotVerified");
        val ^= kStage1NotVerified;
      }
      if (val & kWpOff) {
        ret.push_back("kWpOff");
        val ^= kWpOff;
      }
      if (val & kWpOn) {
        ret.push_back("kWpOn");
        val ^= kWpOn;
      }
      if (val & kStage1) {
        ret.push_back("kStage1");
        val ^= kStage1;
      }
      if (val & kAppl) {
        ret.push_back("kAppl");
        val ^= kAppl;
      }
      if (val & kSpiVerified) {
        ret.push_back("kSpiVerified");
        val ^= kSpiVerified;
      }
      if (val & kSpiNotVerified) {
        ret.push_back("kSpiNotVerified");
        val ^= kSpiNotVerified;
      }
      if (val) {
        ret.push_back(base::StringPrintf("0x%x", val));
      }
      return base::JoinString(ret, "|");

    case HpsReg::kBankReady:
      if (val & ENUM_BIT(HpsBank::kMcuFlash)) {
        ret.push_back("kMcuFlash");
        val ^= ENUM_BIT(HpsBank::kMcuFlash);
      }
      if (val & ENUM_BIT(HpsBank::kSpiFlash)) {
        ret.push_back("kSpiFlash");
        val ^= ENUM_BIT(HpsBank::kSpiFlash);
      }
      if (val & ENUM_BIT(HpsBank::kSocRom)) {
        ret.push_back("kSocRom");
        val ^= ENUM_BIT(HpsBank::kSocRom);
      }
      if (val) {
        ret.push_back(base::StringPrintf("0x%x", val));
      }
      return base::JoinString(ret, "|");

    case HpsReg::kError:
      if (val & RError::kBufORun) {
        ret.push_back("kBufORun");
        val ^= RError::kBufORun;
      }
      if (val & RError::kBufNAvail) {
        ret.push_back("kBufNAvail");
        val ^= RError::kBufNAvail;
      }
      if (val & RError::kI2cBadReq) {
        ret.push_back("kI2cBadReq");
        val ^= RError::kI2cBadReq;
      }
      if (val & RError::kSpiFlash) {
        ret.push_back("kSpiFlash");
        val ^= RError::kSpiFlash;
      }
      if (val & RError::kCamera) {
        ret.push_back("kCamera");
        val ^= RError::kCamera;
      }
      if (val & RError::kI2cORun) {
        ret.push_back("kI2cORun");
        val ^= RError::kI2cORun;
      }
      if (val & RError::kI2cBErr) {
        ret.push_back("kI2cBErr");
        val ^= RError::kI2cBErr;
      }
      if (val & RError::kPanic) {
        ret.push_back("kPanic");
        val ^= RError::kPanic;
      }
      if (val & RError::kMcuFlash) {
        ret.push_back("kMcuFlash");
        val ^= RError::kMcuFlash;
      }
      if (val & RError::kI2cURun) {
        ret.push_back("kI2cURun");
        val ^= RError::kI2cURun;
      }
      if (val & RError::kSpiNotVer) {
        ret.push_back("kSpiNotVerified");
        val ^= RError::kSpiNotVer;
      }
      if (val) {
        ret.push_back(base::StringPrintf("0x%x", val));
      }
      return base::JoinString(ret, "|");

    case HpsReg::kApplVers:
    case HpsReg::kFeatEn:
    case HpsReg::kFeature0:
    case HpsReg::kFeature1:
    case HpsReg::kFirmwareVersionHigh:
    case HpsReg::kFirmwareVersionLow:
    case HpsReg::kHwRev:
    case HpsReg::kMagic:
    case HpsReg::kMax:
    case HpsReg::kSysCmd:
      return "";
  }
}

}  // namespace hps
