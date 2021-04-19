// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Device access interface.
 */
#ifndef HPS_LIB_DEV_H_
#define HPS_LIB_DEV_H_

#include <stdint.h>
#include <vector>

#include "hps/lib/hps_reg.h"

#define RETRIES 5

namespace hps {

class DevInterface {
 public:
  virtual ~DevInterface() {}
  /*
   * The size of the vector indicates the amount of data to read.
   * Returns true on successful read, false on error. The
   * size of the vector remains unchanged. In the event of an error,
   * the contents may have been modified.
   */
  virtual bool read(uint8_t cmd, std::vector<uint8_t>* data) = 0;
  /*
   * Write the data in the vector to the device.
   * Returns true on successful write, false on error.
   */
  virtual bool write(uint8_t cmd, const std::vector<uint8_t>& data) = 0;
  /*
   * Read 1 register.
   * Returns value read, or -1 for error.
   */
  int readReg(int r) {
    std::vector<uint8_t> res(2);

    for (int i = 0; i < RETRIES; i++) {
      if (this->read(I2cReg(r), &res)) {
        return (static_cast<int>(res[0]) << 8) | static_cast<int>(res[1]);
      }
    }
    return -1;
  }
  /*
   * Write 1 register.
   * Returns false on failure.
   */
  bool writeReg(int r, uint16_t data) {
    std::vector<uint8_t> buf(2);

    buf[0] = data >> 8;
    buf[1] = data & 0xFF;
    for (int i = 0; i < RETRIES; i++) {
      if (this->write(I2cReg(r), buf)) {
        return true;
      }
    }
    return false;
  }
};

}  // namespace hps

#endif  // HPS_LIB_DEV_H_
