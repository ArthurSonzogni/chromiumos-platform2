// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * I2C device handler.
 */

#include <vector>

#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>

#include "hps/lib/i2c.h"

namespace hps {

I2CDev::I2CDev(int i2c_bus, int addr) : bus_(i2c_bus), address_(addr), fd_(-1) {
  this->name_ = "/dev/i2c-" + std::to_string(bus_);
}

int I2CDev::Open() {
  this->fd_ = open(this->name_.c_str(), O_RDWR);
  if (this->fd_ < 0) {
    perror(this->name_.c_str());
  }
  return this->fd_;
}

bool I2CDev::Read(uint8_t cmd, uint8_t* data, size_t len) {
  struct i2c_msg m[2];

  m[0].addr = this->address_;
  m[0].flags = 0;
  m[0].len = sizeof(cmd);
  m[0].buf = &cmd;
  m[1].addr = this->address_;
  m[1].flags = I2C_M_RD;
  m[1].len = len;
  m[1].buf = data;
  return this->Ioc(m, sizeof(m) / sizeof(m[0]));
}

bool I2CDev::Write(uint8_t cmd, const uint8_t* data, size_t len) {
  struct i2c_msg m[2];

  m[0].addr = this->address_;
  m[0].flags = 0;
  m[0].len = sizeof(cmd);
  m[0].buf = &cmd;
  m[1].addr = this->address_;
  m[1].flags = 0;
  m[1].len = len;
  m[1].buf = const_cast<uint8_t*>(data);
  return this->Ioc(m, sizeof(m) / sizeof(m[0]));
}

bool I2CDev::Ioc(struct i2c_msg* msg, size_t count) {
  struct i2c_rdwr_ioctl_data ioblk;

  ioblk.msgs = msg;
  ioblk.nmsgs = count;
  int ret = ioctl(this->fd_, I2C_RDWR, &ioblk);
  if (ret < 0) {
    perror(this->name_.c_str());
  }
  return ret != -1;
}

}  // namespace hps
