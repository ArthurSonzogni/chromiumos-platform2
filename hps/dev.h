// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Device access interface.
 */
#ifndef HPS_DEV_H_
#define HPS_DEV_H_

#include <cstddef>
#include <cstdint>

#include <base/compiler_specific.h>

#include <hps/hps_reg.h>

namespace hps {

class DevInterface {
 public:
  virtual ~DevInterface() {}
  /*
   * Returns true on successful read, false on error.
   * In the event of an error, the contents may have been modified.
   */
  virtual bool Read(uint8_t cmd, uint8_t* data, size_t len) WARN_UNUSED_RESULT;

  /*
   * Write the data to the device.
   * Returns true on successful write, false on error.
   */
  virtual bool Write(uint8_t cmd,
                     const uint8_t* data,
                     size_t len) WARN_UNUSED_RESULT;

  /*
   * Read 1 register.
   * Returns value read, or -1 for error.
   */
  virtual int ReadReg(HpsReg r) WARN_UNUSED_RESULT;

  /*
   * Write 1 register.
   * Returns false on failure.
   */
  virtual bool WriteReg(HpsReg r, uint16_t data) WARN_UNUSED_RESULT;

  /*
   * Return the maximum download block size (in bytes).
   * This value is the actual data to be written, not including the
   * write command byte or the 4 byte address header.
   * This must be a power of two.
   */
  virtual size_t BlockSizeBytes();

 private:
  /*
   * Device specific implementations of the Read/Write methods,
   * with the same contract.
   */
  virtual bool ReadDevice(uint8_t cmd, uint8_t* data, size_t len) = 0;
  virtual bool WriteDevice(uint8_t cmd, const uint8_t* data, size_t len) = 0;
};

}  // namespace hps

#endif  // HPS_DEV_H_
