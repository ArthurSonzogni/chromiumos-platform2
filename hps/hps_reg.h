// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Definitions for HPS host interface.
//
#ifndef HPS_HPS_REG_H_
#define HPS_HPS_REG_H_

#include <cstdint>

#define BIT(b) (1ULL << (b))

namespace hps {

// Memory bank numbers for the download operation
enum class HpsBank {
  kMcuFlash = 0,
  kSpiFlash = 1,
  kSocRom = 2,
};

// Register numbers for HPS module interface.
enum class HpsReg : uint8_t {
  kMagic = 0,
  kHwRev = 1,
  kSysStatus = 2,
  kSysCmd = 3,
  kApplVers = 4,
  kBankReady = 5,
  kError = 6,
  kFeatEn = 7,
  kF1 = 8,
  kF2 = 9,
  kFirmwareVersionHigh = 10,
  kFirmwareVersionLow = 11,
  kMax = 127,
};

// Register 2 (RO) - System status register.
enum R2 : uint16_t {
  kOK = BIT(0),
  kFault = BIT(1),
  kApplVerified = BIT(2),
  kApplNotVerified = BIT(3),
  kWpOn = BIT(4),
  kWpOff = BIT(5),
  // Unused          BIT(6),
  // Unused          BIT(7),
  kStage1 = BIT(8),        // Stage1 running
  kAppl = BIT(9),          // Application running
  kSpiVerified = BIT(10),  // SPI flash verified
  kSpiNotVerified = BIT(11),
};

// Register 3 (WO) - System command register.
enum R3 : uint16_t {
  kReset = BIT(0),
  kLaunch = BIT(1),
  kEnable = BIT(2),
};

// Register 7 (RW) - Feature enable bit mask.
enum R7 : uint16_t {
  kFeature1Enable = BIT(0),
  kFeature2Enable = BIT(1),
};

// Feature result registers (R8 & R9).
enum RFeat : uint16_t {
  kValid = BIT(15),  // Feature result is valid.
};

enum RError : uint16_t {
  kSpiFlash = BIT(6),
  kCamera = BIT(5),
  kI2cORun = BIT(4),
  kI2cBErr = BIT(3),
  kPanic = BIT(2),
  kMcuFlash = BIT(1),
  kI2cURun = BIT(0),
};

inline constexpr uint16_t kHpsMagic = 0x9df2;
inline constexpr int kFeatures = 2;  // Maximum of 2 features at this stage.

// The interface allows up to 64 banks, but only 16 are
// usable at this stage because of the requirement to check
// if the bank is ready via a register.

inline constexpr int kNumBanks = 16;

inline uint8_t I2cMemWrite(int bank) {
  return (bank % kNumBanks) | 0;
}

inline uint8_t I2cReg(HpsReg reg) {
  return static_cast<uint8_t>(reg) | 0x80U;
}

struct FeatureResult {
  uint8_t inference_result;
  bool valid;
};

}  // namespace hps

#endif  // HPS_HPS_REG_H_
