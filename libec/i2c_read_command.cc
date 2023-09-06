// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/i2c_read_command.h"

#include <vector>

#include <base/containers/span.h>
#include <base/notreached.h>

namespace ec {

uint32_t I2cReadCommand::Data() const {
  CHECK(RespData().size() == read_len_)
      << "Unexpected response size. Expected " << read_len_ << ", got"
      << RespData().size() << ".";
  if (read_len_ == 1) {
    return RespData()[0];
  }
  if (read_len_ == 2 || read_len_ == 4) {
    // Copy data out of place to align memory addresses.
    base::span<const uint8_t> raw_data = RespData();
    std::vector<uint8_t> aligned_data(raw_data.begin(), raw_data.end());
    if (read_len_ == 2) {
      return *reinterpret_cast<const uint16_t*>(aligned_data.data());
    }
    if (read_len_ == 4) {
      return *reinterpret_cast<const uint32_t*>(aligned_data.data());
    }
  }
  NOTREACHED();
  return 0;
}

}  // namespace ec
