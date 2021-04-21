// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Main HPS class.
 */

#include <vector>

#include <base/threading/thread.h>
#include <base/time/time.h>

#include "hps/lib/hps.h"

namespace hps {

static const int kBlock = 256;
static const int kTimeoutMs = 250;  // Bank ready timeout
static const int kPollMs = 5;       // Delay time for poll.

void HPS::Init() {
  this->state_ = State::init;
}

/*
 * Download data to the bank specified.
 * The HPa/HostS I2C Interface Memory Write is used.
 * The format of the data is:
 *    cmd byte
 *    32 bits of address in big endian format
 *    data
 */
int HPS::Download(int bank, uint32_t address, std::ifstream& source) {
  if (bank < 0 || bank >= kNumBanks) {
    return -1;
  }
  std::vector<uint8_t> buf(kBlock + sizeof(uint32_t));
  int bytes = 0;
  int rd;
  do {
    // Wait until the bank is ready.
    int tout = 0;
    for (;;) {
      int result = this->device_->readReg(HpsReg::kBankReady);
      if (result < 0) {
        return bytes;
      }
      if (result & (1 << bank)) {
        break;
      }
      // If timed out, finish the write.
      if (tout >= kTimeoutMs) {
        return bytes;
      }
      base::PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(kPollMs));
      tout += kPollMs;
    }
    /*
     * Leave room for a 32 bit address at the start of the block
     * to be written.
     * The address is updated for each block to indicate
     * where this block is to be written.
     */
    buf.resize(kBlock + sizeof(uint32_t));
    buf[0] = address >> 24;
    buf[1] = address >> 16;
    buf[2] = address >> 8;
    buf[3] = address;
    rd = source.readsome(reinterpret_cast<char*>(&buf[sizeof(uint32_t)]),
                         kBlock);
    if (rd > 0) {
      buf.resize(rd + sizeof(uint32_t));
      if (!this->device_->write(I2cMemWrite(bank), buf)) {
        break;
      }
      bytes += rd;
      address += rd;
    }
  } while (rd > 0);  // A read returning 0 indicates EOF.
  return bytes;
}

}  // namespace hps
