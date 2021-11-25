// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * I2C device handler.
 */
#ifndef HPS_HAL_I2C_H_
#define HPS_HAL_I2C_H_

#include <memory>

#include <stdint.h>
#include <string>

#include "hps/dev.h"

struct i2c_msg;

namespace hps {

class I2CDev : public DevInterface {
 public:
  ~I2CDev() override {}
  int Open();
  bool ReadDevice(uint8_t cmd, uint8_t* data, size_t len) override;
  bool WriteDevice(uint8_t cmd, const uint8_t* data, size_t len) override;
  static std::unique_ptr<DevInterface> Create(const std::string&,
                                              uint8_t address);

 private:
  I2CDev(const std::string& bus, uint8_t address);
  bool Ioc(struct i2c_msg* msg, size_t count);
  const std::string bus_;
  uint8_t address_;
  int fd_;
};

}  // namespace hps

#endif  // HPS_HAL_I2C_H_
