// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FLASH_ERASE_PARAMS_H_
#define LIBEC_FLASH_ERASE_PARAMS_H_

#include <array>

#include "libec/ec_command.h"

namespace ec {
namespace flash_erase {

// This matches struct ec_params_flash_erase_v1, except "cmd" is renamed to
// "action" in order to work with the EcCommandAsync template, which expects
// the name to be "action". See the test file that validates the fields match
// the original.
struct Params_v1 {
  uint8_t action;
  uint8_t reserved;
  uint16_t flag;
  struct ec_params_flash_erase params;
};

}  // namespace flash_erase
}  // namespace ec

#endif  // LIBEC_FLASH_ERASE_PARAMS_H_
