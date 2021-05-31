// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Device access interface common functions.
 */
#include "hps/dev.h"
#include "hps/hps_reg.h"

namespace hps {

/*
 * TODO(amcrae): It is questionable whether this layer should be using
 * retries, since a retry shim layer can be added separately.
 */
static const int kIoRetries = 5;

/*
 * Read 1 register.
 * Returns value read, or -1 for error.
 */
int DevInterface::ReadReg(int r) {
  uint8_t res[2];

  for (int i = 0; i < kIoRetries; i++) {
    if (this->Read(I2cReg(r), res, sizeof(res))) {
      return (static_cast<int>(res[0]) << 8) | static_cast<int>(res[1]);
    }
  }
  return -1;
}

/*
 * Write 1 register.
 * Returns false on failure.
 */
bool DevInterface::WriteReg(int r, uint16_t data) {
  uint8_t buf[2];

  buf[0] = data >> 8;
  buf[1] = data & 0xFF;
  for (int i = 0; i < kIoRetries; i++) {
    if (this->Write(I2cReg(r), buf, sizeof(buf))) {
      return true;
    }
  }
  return false;
}

}  // namespace hps
