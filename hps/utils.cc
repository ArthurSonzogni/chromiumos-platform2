// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/sys_byteorder.h>

#include <hps/hps_reg.h>
#include <hps/utils.h>

namespace hps {

#define ENUM_BIT(e) BIT(static_cast<int>(e))

bool ReadVersionFromFile(const base::FilePath& path, uint32_t* version) {
  std::string file_contents;
  if (!base::ReadFileToString(path, &file_contents)) {
    PLOG(ERROR) << "ReadVersionFromFile: \"" << path << "\"";
    return false;
  }
  base::TrimWhitespaceASCII(file_contents, base::TRIM_ALL, &file_contents);
  if (!base::StringToUint(file_contents, version)) {
    LOG(ERROR) << "ReadVersionFromFile: \"" << path
               << "\": file does not contain a valid integer version";
    return false;
  }
  return true;
}

constexpr std::initializer_list<RegInfo> REGISTERS = {
    {.num = HpsReg::kMagic, .name = "kMagic", .readable = true},
    {.num = HpsReg::kHwRev, .name = "kHwRev", .readable = true},
    {.num = HpsReg::kSysStatus, .name = "kSysStatus", .readable = true},
    {.num = HpsReg::kSysCmd, .name = "kSysCmd", .readable = false},
    {.num = HpsReg::kApplVers, .name = "kApplVers", .readable = true},
    {.num = HpsReg::kBankReady, .name = "kBankReady", .readable = true},
    {.num = HpsReg::kError, .name = "kError", .readable = true},
    {.num = HpsReg::kFeatEn, .name = "kFeatEn", .readable = true},
    {.num = HpsReg::kFeature0, .name = "kFeature0", .readable = true},
    {.num = HpsReg::kFeature1, .name = "kFeature1", .readable = true},
    {.num = HpsReg::kFirmwareVersionHigh,
     .name = "kFirmwareVersionHigh",
     .readable = true},
    {.num = HpsReg::kFirmwareVersionLow,
     .name = "kFirmwareVersionLow",
     .readable = true},
    {.num = HpsReg::kFpgaBootCount, .name = "kFpgaBootCount", .readable = true},
    {.num = HpsReg::kFpgaLoopCount, .name = "kFpgaLoopCount", .readable = true},
    {.num = HpsReg::kFpgaRomVersion,
     .name = "kFpgaRomVersion",
     .readable = true},
    {.num = HpsReg::kSpiFlashStatus,
     .name = "kSpiFlashStatus",
     .readable = true},
    {.num = HpsReg::kDebugIdx, .name = "kDebugIdx", .readable = true},
    {.num = HpsReg::kDebugVal, .name = "kDebugVal", .readable = true},
    {.num = HpsReg::kCameraConfig, .name = "kCameraConfig", .readable = true},
    {.num = HpsReg::kStartCameraTest,
     .name = "kStartCameraTest",
     .readable = false},
    {.num = HpsReg::kOptionBytesConfig,
     .name = "kOptionBytesConfig",
     .readable = true},
    {.num = HpsReg::kPartIds, .name = "kPartIds", .readable = true},
    {.num = HpsReg::kPreviousCrashMessage,
     .name = "kPreviousCrashMessage",
     .readable = true},
};

std::optional<RegInfo> HpsRegInfo(int reg) {
  auto ret = std::find_if(
      REGISTERS.begin(), REGISTERS.end(),
      [&](RegInfo info) { return static_cast<int>(info.num) == reg; });
  if (ret != REGISTERS.end()) {
    return *ret;
  }
  return std::nullopt;
}

std::optional<RegInfo> HpsRegInfo(HpsReg reg) {
  return HpsRegInfo(static_cast<int>(reg));
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
      if (val & kDeprecatedAVerify) {
        ret.push_back("kDeprecatedAVerify");
        val ^= kDeprecatedAVerify;
      }
      if (val & kStage0) {
        ret.push_back("kStage0");
        val ^= kStage0;
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
      if (val & kCmdInProgress) {
        ret.push_back("kCmdInProgress");
        val ^= kCmdInProgress;
      }
      if (val & kStage0Locked) {
        ret.push_back("kStage0Locked");
        val ^= kStage0Locked;
      }
      if (val & kStage0PermLocked) {
        ret.push_back("kStage0PermLocked");
        val ^= kStage0PermLocked;
      }
      if (val & kOneTimeInit) {
        ret.push_back("kOneTimeInit");
        val ^= kOneTimeInit;
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
      switch (val) {
        case RError::kNone:
          return "kNone";
        case RError::kHostI2cUnderrun:
          return "kHostI2cUnderrun";
        case RError::kMcuFlashWriteError:
          return "kMcuFlashWriteError";
        case RError::kPanic:
          return "kPanic";
        case RError::kHostI2cBusError:
          return "kHostI2cBusError";
        case RError::kHostI2cOverrun:
          return "kHostI2cOverrun";
        case RError::kCamera:
          return "kCamera";
        case RError::kSpiFlash:
          return "kSpiFlash";
        case RError::kHostI2cBadRequest:
          return "kHostI2cBadRequest";
        case RError::kBufferNotAvailable:
          return "kBufferNotAvailable";
        case RError::kBufferOverrun:
          return "kBufferOverrun";
        case RError::kSpiFlashNotVerified:
          return "kSpiFlashNotVerified";
        case RError::kTfliteFailure:
          return "kTfliteFailure";
        case RError::kSelfTestFailed:
          return "kSelfTestFailed";
        case RError::kFpgaMcuCommError:
          return "kFpgaMcuCommError";
        case RError::kFpgaTimeout:
          return "kFpgaTimeout";
        case RError::kStage1NotFound:
          return "kStage1NotFound";
        case RError::kStage1TooOld:
          return "kStage1TooOld";
        case RError::kStage1InvalidSignature:
          return "kStage1InvalidSignature";
        case RError::kInternal:
          return "kInternal";
        case RError::kMcuFlashEcc:
          return "kMcuFlashEcc";
        case RError::kMcuNmi:
          return "kMcuNmi";
        default:
          return base::StringPrintf("0x%04x", val);
      }

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
    case HpsReg::kFpgaBootCount:
    case HpsReg::kFpgaLoopCount:
    case HpsReg::kFpgaRomVersion:
    case HpsReg::kSpiFlashStatus:
    case HpsReg::kDebugIdx:
    case HpsReg::kDebugVal:
    case HpsReg::kCameraConfig:
    case HpsReg::kStartCameraTest:
    case HpsReg::kOptionBytesConfig:
    case HpsReg::kPartIds:
    case HpsReg::kPreviousCrashMessage:
      return "";
  }
}

}  // namespace hps
