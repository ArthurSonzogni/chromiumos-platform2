// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/stl_util.h>
#include <chromeos/ec/ec_commands.h>

#include "libec/ec_command.h"
#include "libec/flash_protect_command.h"

namespace ec {

namespace flash_protect {
std::ostream& operator<<(std::ostream& os, flash_protect::Flags r) {
  os << base::to_underlying(r);
  return os;
}
}  // namespace flash_protect

FlashProtectCommand::FlashProtectCommand(flash_protect::Flags flags,
                                         flash_protect::Flags mask)
    : EcCommand(EC_CMD_FLASH_PROTECT, EC_VER_FLASH_PROTECT) {
  Req()->flags = base::to_underlying(flags);
  Req()->mask = base::to_underlying(mask);
}

/**
 * @return string names of set flags
 */
std::string FlashProtectCommand::ParseFlags(flash_protect::Flags flags) {
  std::string output;
  if ((flags & flash_protect::Flags::kRoAtBoot) !=
      flash_protect::Flags::kNone) {
    output += "RO_AT_BOOT  ";
  }
  if ((flags & flash_protect::Flags::kRoNow) != flash_protect::Flags::kNone) {
    output += "RO_NOW  ";
  }
  if ((flags & flash_protect::Flags::kAllNow) != flash_protect::Flags::kNone) {
    output += "ALL_NOW  ";
  }
  if ((flags & flash_protect::Flags::kGpioAsserted) !=
      flash_protect::Flags::kNone) {
    output += "GPIO_ASSERTED  ";
  }
  if ((flags & flash_protect::Flags::kErrorStuck) !=
      flash_protect::Flags::kNone) {
    output += "ERROR_STUCK  ";
  }
  if ((flags & flash_protect::Flags::kErrorInconsistent) !=
      flash_protect::Flags::kNone) {
    output += "ERROR_INCONSISTENT  ";
  }
  if ((flags & flash_protect::Flags::kAllAtBoot) !=
      flash_protect::Flags::kNone) {
    output += "ALL_AT_BOOT  ";
  }
  if ((flags & flash_protect::Flags::kRwAtBoot) !=
      flash_protect::Flags::kNone) {
    output += "RW_AT_BOOT  ";
  }
  if ((flags & flash_protect::Flags::kRwNow) != flash_protect::Flags::kNone) {
    output += "RW_NOW  ";
  }
  if ((flags & flash_protect::Flags::kRollbackAtBoot) !=
      flash_protect::Flags::kNone) {
    output += "ROLLBACK_AT_BOOT  ";
  }
  if ((flags & flash_protect::Flags::kRollbackNow) !=
      flash_protect::Flags::kNone) {
    output += "ROLLBACK_NOW  ";
  }

  return output;
}

flash_protect::Flags FlashProtectCommand::GetFlags() const {
  return static_cast<flash_protect::Flags>(Resp()->flags);
}

flash_protect::Flags FlashProtectCommand::GetValidFlags() const {
  return static_cast<flash_protect::Flags>(Resp()->valid_flags);
}

flash_protect::Flags FlashProtectCommand::GetWritableFlags() const {
  return static_cast<flash_protect::Flags>(Resp()->writable_flags);
}

}  // namespace ec
