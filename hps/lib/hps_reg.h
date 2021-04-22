// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Definitions for HPS host interface.
 */
#ifndef HPS_LIB_HPS_REG_H_
#define HPS_LIB_HPS_REG_H_

namespace hps {

enum HpsReg {
  kMagic = 0,
  kHwRev = 1,
  kSysStatus = 2,
  kSysCmd = 3,
  kApplVers = 4,
  kBankReady = 5,
  kError = 6,
  kMax = 127,
  kNumRegs = kMax + 1,
};

enum R2 {
  kOK = 1 << 0,
  kFault = 1 << 1,
  kApplVerified = 1 << 2,
  kApplNotVerified = 1 << 3,
  kWpOff = 1 << 4,
  kWpOn = 1 << 5,
  // Unused           = 1<<6,
  // Unused           = 1<<7,
  kStage1 = 1 << 8,        // Stage1 running
  kAppl = 1 << 9,          // Application running
  kSpiVerified = 1 << 10,  // SPI flash verified
  kSpiNotVerified = 1 << 11,
};

enum R3 {
  kReset = 1 << 0,
  kLaunch = 1 << 1,
  kEnable = 1 << 2,
};

inline constexpr uint16_t kHpsMagic = 0x9df2;
/*
 * The interface allows up to 64 banks, but only 16 are
 * usable at this stage because of the requirement to check
 * if the bank is ready via a register.
 */
inline constexpr int kNumBanks = 16;

inline uint8_t I2cMemWrite(int bank) {
  return (bank % kNumBanks) | 0;
}

inline uint8_t I2cReg(int reg) {
  return (reg % HpsReg::kNumRegs) | 0x80;
}

}  // namespace hps

#endif  // HPS_LIB_HPS_REG_H_
