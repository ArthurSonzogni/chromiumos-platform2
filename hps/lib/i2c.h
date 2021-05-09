// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * I2C device handler.
 */
#ifndef HPS_LIB_I2C_H_
#define HPS_LIB_I2C_H_

#include <vector>

#include <stdint.h>
#include <string>

#include "hps/lib/dev.h"

struct i2c_msg;

namespace hps {

class I2CDev : public DevInterface {
 public:
  I2CDev(int bus, int address);
  ~I2CDev() {}
  int Open();
  bool Read(uint8_t cmd, uint8_t* data, size_t len) override;
  bool Write(uint8_t cmd, const uint8_t* data, size_t len) override;

 private:
  bool Ioc(struct i2c_msg* msg, size_t count);
  int bus_;
  int address_;
  int fd_;
  std::string name_;
};

}  // namespace hps

#endif  // HPS_LIB_I2C_H_
