// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FLASH_PROTECT_COMMAND_H_
#define LIBEC_FLASH_PROTECT_COMMAND_H_

#include <memory>
#include <string>

#include <brillo/brillo_export.h>
#include "libec/ec_command.h"

namespace ec {

class BRILLO_EXPORT FlashProtectCommand
    : public EcCommand<struct ec_params_flash_protect,
                       struct ec_response_flash_protect> {
 public:
  FlashProtectCommand(const uint32_t flags, const uint32_t mask);
  ~FlashProtectCommand() override = default;

  static std::string ParseFlags(uint32_t flags);
};

}  // namespace ec

#endif  // LIBEC_FLASH_PROTECT_COMMAND_H_
