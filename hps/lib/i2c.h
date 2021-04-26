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
  bool read(uint8_t cmd, uint8_t* data, uint len) override;
  bool write(uint8_t cmd, const uint8_t* data, uint len) override;

 private:
  bool ioc(struct i2c_msg* msg, uint count);
  int bus;
  int address;
  int fd;
  std::string name;
};

}  // namespace hps

#endif  // HPS_LIB_I2C_H_
