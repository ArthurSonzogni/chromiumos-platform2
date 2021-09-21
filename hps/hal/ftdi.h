// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Access via FTDI device, using open source library.
 */
#ifndef HPS_HAL_FTDI_H_
#define HPS_HAL_FTDI_H_

#include <memory>
#include <string>
#include <vector>

#include <libftdi1/ftdi.h>

#include "hps/dev.h"

namespace hps {

class Ftdi : public DevInterface {
 public:
  void Close();
  bool ReadDevice(uint8_t cmd, uint8_t* data, size_t lem) override;
  bool WriteDevice(uint8_t cmd, const uint8_t* data, size_t lem) override;
  static std::unique_ptr<DevInterface> Create(uint8_t address,
                                              uint32_t speedKHz);

 private:
  explicit Ftdi(uint8_t addr) : address_(addr << 1) {}
  bool Init(uint32_t speedKHz);
  bool Check(bool cond, const char* tag);
  void Reset();
  // bool I2CRead(std::vector<uint8_t>* data);
  bool SendByte(uint8_t data, std::vector<uint8_t>* b);
  bool ReadByte(uint8_t* result, bool nak);
  bool GetRawBlock(size_t count, std::vector<uint8_t>* input);
  bool GetRaw(std::vector<uint8_t>* get);
  size_t PutRaw(const std::vector<uint8_t>& output);
  void Dump();
  uint8_t address_;
  struct ftdi_context context_;
  std::string descr_;
  std::string manuf_;
  std::string serial_;
};

}  // namespace hps

#endif  // HPS_HAL_FTDI_H_
