// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FLASH_INFO_PARAMS_H_
#define LIBEC_FLASH_INFO_PARAMS_H_

#include <array>

#include "libec/ec_command.h"

namespace ec::flash_info {

// We cannot use "struct ec_response_flash_info_2" directly in the
// FlashInfoCommand class because the "banks" member is a variable length array.
// "Header" includes everything from that struct except "banks". A test
// validates that the size of the two structs is the same to avoid any
// divergence.
struct Header {
  uint32_t flash_size;
  /* Flags; see EC_FLASH_INFO_* */
  uint32_t flags;
  /* Maximum size to use to send data to write to the EC. */
  uint32_t write_ideal_size;
  /* Number of banks present in the EC. */
  uint16_t num_banks_total;
  /* Number of banks described in banks array. */
  uint16_t num_banks_desc;
};

// Allocates space for the flash bank response.
struct Params_v2 {
  struct Header info {};
  ArrayData<struct ec_flash_bank, struct Header> banks{};
};

}  // namespace ec::flash_info

bool operator==(const struct ec_flash_bank& lhs,
                const struct ec_flash_bank& rhs);

#endif  // LIBEC_FLASH_INFO_PARAMS_H_
