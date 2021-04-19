// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Fake device for HPS testing.
 */
#ifndef HPS_LIB_FAKE_DEV_H_
#define HPS_LIB_FAKE_DEV_H_

#include <vector>

#include <base/synchronization/lock.h>

#include "hps/lib/dev.h"
#include "hps/lib/hps_reg.h"

namespace hps {

class FakeDev : public DevInterface {
 public:
  FakeDev() {
    for (int i = 0; i < HpsReg::kNumRegs; i++) {
      this->regs_[i] = 0;
    }
    regs_[HpsReg::kMagic] = kHpsMagic;
    regs_[HpsReg::kBankReady] = 0x0001;
  }
  virtual ~FakeDev() {}
  // Device interface
  bool read(uint8_t cmd, std::vector<uint8_t>* data) override;
  bool write(uint8_t cmd, const std::vector<uint8_t>& data) override;
  // Control methods for fake.
  void SetReg(int reg, uint16_t value) {
    if (reg < 0 || reg > HpsReg::kMax) {
      return;
    }
    base::AutoLock l(this->lock_);
    this->regs_[reg] = value;
  }

 private:
  base::Lock lock_;
  uint16_t regs_[HpsReg::kNumRegs];
};

}  // namespace hps

#endif  // HPS_LIB_FAKE_DEV_H_
