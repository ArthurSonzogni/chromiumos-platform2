// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * UART based device handler.
 */
#ifndef HPS_LIB_UART_H_
#define HPS_LIB_UART_H_

#include <vector>

#include <stdint.h>

#include "hps/lib/dev.h"

namespace hps {

class Uart : public DevInterface {
 public:
  explicit Uart(const char* device);
  virtual ~Uart();
  int Open();
  bool Read(uint8_t cmd, uint8_t* data, size_t len) override;
  bool Write(uint8_t cmd, const uint8_t* data, size_t len) override;

 private:
  const char* device_;
  int fd_;
};

}  // namespace hps

#endif  // HPS_LIB_UART_H_
