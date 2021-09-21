// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * UART interconnection device handler.
 */
#include "hps/hal/uart.h"

#include <utility>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include <base/check.h>
#include <base/check_op.h>

namespace hps {

Uart::Uart(const char* device) : device_(device), fd_(-1) {}

Uart::~Uart() {
  if (this->fd_ >= 0) {
    close(this->fd_);
  }
}

int Uart::Open() {
  this->fd_ = open(this->device_, O_RDWR);
  if (this->fd_ < 0) {
    perror(this->device_);
    return -1;
  }
  // Set up serial device: raw I/O, 115200 baud.
  struct termios tios;
  if (tcgetattr(this->fd_, &tios) < 0) {
    perror(this->device_);
    close(this->fd_);
    this->fd_ = -1;
  }
  cfmakeraw(&tios);
  cfsetspeed(&tios, B115200);
  if (tcsetattr(this->fd_, TCSANOW, &tios) < 0) {
    perror(this->device_);
    close(this->fd_);
    this->fd_ = -1;
  }
  return this->fd_;
}

bool Uart::ReadDevice(uint8_t cmd, uint8_t* data, size_t len) {
  if (len >= 127) {
    return false;
  }
  // Send start and byte length of 1 for command.
  uint8_t c = 0x80 | 1;
  if (write(this->fd_, &c, 1) != 1) {
    return false;
  }
  if (write(this->fd_, &cmd, 1) != 1) {
    return false;
  }
  // Now request read.
  c = len;
  if (write(this->fd_, &c, 1) != 1) {
    return false;
  }
  // Read loop to retrieve data.
  while (len > 0) {
    int rd = read(this->fd_, data, len);
    if (rd < 0) {
      perror(this->device_);
      return false;
    }
    data += rd;
    len -= rd;
  }
  // Send stop.
  c = 0;
  if (write(this->fd_, &c, 1) != 1) {
    return false;
  }
  return true;
}

bool Uart::WriteDevice(uint8_t cmd, const uint8_t* data, size_t len) {
  if (len >= 127) {
    return false;
  }
  // Send start and byte count (including command).
  uint8_t c = 0x80 | (len + 1);
  if (write(this->fd_, &c, 1) != 1) {
    return false;
  }
  // Send cmd followed by data.
  if (write(this->fd_, &cmd, 1) != 1) {
    return false;
  }
  if (write(this->fd_, data, len) != len) {
    return false;
  }
  // Send stop.
  c = 0;
  if (write(this->fd_, &c, 1) != 1) {
    return false;
  }
  return true;
}

// Static factory method.
std::unique_ptr<DevInterface> Uart::Create(const char* device) {
  // Use new so that private constructor can be accessed.
  auto dev = std::unique_ptr<Uart>(new Uart(device));
  CHECK_GE(dev->Open(), 0);
  return std::unique_ptr<DevInterface>(std::move(dev));
}

}  // namespace hps
