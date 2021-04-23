// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Main HPS class.
 */
#ifndef HPS_LIB_HPS_H_
#define HPS_LIB_HPS_H_

#include <fstream>
#include <iostream>
#include <memory>
#include <utility>

#include "hps/lib/dev.h"

namespace hps {

class HPS {
 public:
  explicit HPS(std::unique_ptr<DevInterface> dev)
      : device_(std::move(dev)), state_(State::init) {}
  void Init();
  DevInterface* Device() { return this->device_.get(); }
  /*
   * Per the HPS/Host I2C Interface, the bank
   * must be between 0-63 inclusive.
   * The length of data written is returned, or -1
   * in the case of an error.
   */
  int Download(int bank, uint32_t address, std::ifstream& source);

 private:
  enum State {
    init,
    wait_stage0,
    wait_appl,
  };
  std::unique_ptr<DevInterface> device_;
  State state_;
};

}  // namespace hps

#endif  // HPS_LIB_HPS_H_
