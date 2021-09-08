// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FLASH_WRITE_PARAMS_H_
#define LIBEC_FLASH_WRITE_PARAMS_H_

#include <array>

#include "libec/ec_command.h"

namespace ec::flash_write {

// We cannot use "struct ec_params_flash_write" directly in the
// FlashWriteCommand class because the "data" member is a variable length array.
// "Header" includes everything from that struct except "data". A test validates
// that the size of the two structs is the same to avoid any divergence.
struct Header {
  uint32_t offset = 0;
  uint32_t size = 0;
};

struct Params {
  Header req{};
  ArrayData<uint8_t, Header> data{};
};

}  // namespace ec::flash_write

#endif  // LIBEC_FLASH_WRITE_PARAMS_H_
