// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FLASH_PROTECT_COMMAND_H_
#define LIBEC_FLASH_PROTECT_COMMAND_H_

#include <memory>
#include <string>

#include <brillo/brillo_export.h>
#include <brillo/enum_flags.h>
#include "libec/ec_command.h"

namespace ec {

namespace flash_protect {
enum class BRILLO_EXPORT Flags : uint32_t {
  kNone = 0,
  kRoAtBoot = EC_FLASH_PROTECT_RO_AT_BOOT,
  kRoNow = EC_FLASH_PROTECT_RO_NOW,
  kAllNow = EC_FLASH_PROTECT_ALL_NOW,
  kGpioAsserted = EC_FLASH_PROTECT_GPIO_ASSERTED,
  kErrorStuck = EC_FLASH_PROTECT_ERROR_STUCK,
  kErrorInconsistent = EC_FLASH_PROTECT_ERROR_INCONSISTENT,
  kAllAtBoot = EC_FLASH_PROTECT_ALL_AT_BOOT,
  kRwAtBoot = EC_FLASH_PROTECT_RW_AT_BOOT,
  kRwNow = EC_FLASH_PROTECT_RW_NOW,
  kRollbackAtBoot = EC_FLASH_PROTECT_ROLLBACK_AT_BOOT,
  kRollbackNow = EC_FLASH_PROTECT_ROLLBACK_NOW
};
DECLARE_FLAGS_ENUM(Flags);
BRILLO_EXPORT std::ostream& operator<<(std::ostream& os,
                                       flash_protect::Flags r);
}  // namespace flash_protect

class BRILLO_EXPORT FlashProtectCommand
    : public EcCommand<struct ec_params_flash_protect,
                       struct ec_response_flash_protect> {
 public:
  FlashProtectCommand(flash_protect::Flags flags, flash_protect::Flags mask);
  ~FlashProtectCommand() override = default;

  static std::string ParseFlags(flash_protect::Flags flags);

  flash_protect::Flags GetFlags() const;
  flash_protect::Flags GetValidFlags() const;
  flash_protect::Flags GetWritableFlags() const;
};

}  // namespace ec

#endif  // LIBEC_FLASH_PROTECT_COMMAND_H_
