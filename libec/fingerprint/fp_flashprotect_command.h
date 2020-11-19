// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FINGERPRINT_FP_FLASHPROTECT_COMMAND_H_
#define LIBEC_FINGERPRINT_FP_FLASHPROTECT_COMMAND_H_

#include <memory>
#include <string>

#include <brillo/brillo_export.h>
#include "libec/ec_command.h"

namespace ec {

class BRILLO_EXPORT FpFlashProtectCommand
    : public EcCommand<struct ec_params_flash_protect,
                       struct ec_response_flash_protect> {
 public:
  FpFlashProtectCommand()
      : EcCommand(EC_CMD_FLASH_PROTECT, EC_VER_FLASH_PROTECT) {}
  ~FpFlashProtectCommand() override = default;

  static std::unique_ptr<FpFlashProtectCommand> Create(const uint32_t flags,
                                                       const uint32_t mask);

  static std::string ParseFlags(uint32_t flags);

 private:
};

}  // namespace ec

#endif  // LIBEC_FINGERPRINT_FP_FLASHPROTECT_COMMAND_H_
