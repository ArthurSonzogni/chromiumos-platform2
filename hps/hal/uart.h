// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * UART based device handler.
 */
#ifndef HPS_HAL_UART_H_
#define HPS_HAL_UART_H_

#include <memory>

#include <stdint.h>

#include "hps/dev.h"

namespace hps {

class Uart : public DevInterface {
 public:
  ~Uart() override;
  int Open();
  bool ReadDevice(uint8_t cmd, uint8_t* data, size_t len) override;
  bool WriteDevice(uint8_t cmd, const uint8_t* data, size_t len) override;
  static std::unique_ptr<DevInterface> Create(const char* device);

 private:
  explicit Uart(const char* device);
  const char* device_;
  int fd_;
};

}  // namespace hps

#endif  // HPS_HAL_UART_H_
