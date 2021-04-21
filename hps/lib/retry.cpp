// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Retry device handler.
 */
#include <iostream>
#include <vector>

#include <base/threading/thread.h>

#include "hps/lib/retry.h"

namespace hps {

bool RetryDev::read(uint8_t cmd, std::vector<uint8_t>* data) {
  for (int i = 0; i < this->retries_; i++) {
    if (device_->read(cmd, data)) {
      // Success!
      return true;
    }
    base::PlatformThread::Sleep(this->delay_);
  }
  return false;
}

bool RetryDev::write(uint8_t cmd, const std::vector<uint8_t>& data) {
  for (int i = 0; i < this->retries_; i++) {
    if (device_->write(cmd, data)) {
      // Success!
      return true;
    }
    base::PlatformThread::Sleep(this->delay_);
  }
  return false;
}

}  // namespace hps
