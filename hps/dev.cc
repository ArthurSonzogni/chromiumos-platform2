// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Device access interface common functions.
 */

#include "hps/dev.h"

#include "base/logging.h"
#include <base/strings/stringprintf.h>
#include "hps/hps_reg.h"
#include "hps/utils.h"

namespace hps {

bool DevInterface::Read(uint8_t cmd, uint8_t* data, size_t len) {
  if (this->ReadDevice(cmd, data, len)) {
    VLOG(2) << base::StringPrintf("Read: cmd: 0x%x len: %zd OK", cmd, len);
    return true;
  }
  VLOG(2) << base::StringPrintf("Read: cmd: 0x%x len: %zd FAILED", cmd, len);
  return false;
}

bool DevInterface::Write(uint8_t cmd, const uint8_t* data, size_t len) {
  if (this->WriteDevice(cmd, data, len)) {
    VLOG(2) << base::StringPrintf("Write: cmd: 0x%x len: %zd OK", cmd, len);
    return true;
  }
  VLOG(2) << base::StringPrintf("Write: cmd: 0x%x len: %zd FAILED", cmd, len);
  return false;
}

/*
 * Read 1 register.
 * Returns value read, or -1 for error.
 */
std::optional<uint16_t> DevInterface::ReadReg(HpsReg r) {
  uint8_t res[2];

  // TODO(evanbenn) MCP hal requires a retry on kBankReady
  // b/191716856
  for (int i = 0; i < 2; i++) {
    if (this->ReadDevice(I2cReg(r), res, sizeof(res))) {
      uint16_t ret = static_cast<uint16_t>(res[0] << 8) | res[1];
      VLOG(2) << base::StringPrintf("ReadReg: %s : 0x%.4x OK",
                                    HpsRegToString(r), ret);
      return ret;
    }
  }
  VLOG(2) << "ReadReg: " << HpsRegToString(r) << " FAILED";
  return std::nullopt;
}

/*
 * Write 1 register.
 * Returns false on failure.
 */
bool DevInterface::WriteReg(HpsReg r, uint16_t data) {
  uint8_t buf[2];

  buf[0] = data >> 8;
  buf[1] = data & 0xFF;

  if (this->WriteDevice(I2cReg(r), buf, sizeof(buf))) {
    VLOG(2) << base::StringPrintf("WriteReg: %s : 0x%.4x OK", HpsRegToString(r),
                                  data);
    return true;
  } else {
    VLOG(2) << base::StringPrintf("WriteReg: %s : 0x%.4x FAILED",
                                  HpsRegToString(r), data);
    return false;
  }
}

/*
 * Return the maximum download block size (in bytes).
 * Default is 256 bytes.
 */
size_t DevInterface::BlockSizeBytes() {
  return 256;
}

}  // namespace hps
