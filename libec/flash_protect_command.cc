// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <base/memory/ptr_util.h>
#include <chromeos/ec/ec_commands.h>

#include "libec/ec_command.h"
#include "libec/flash_protect_command.h"

namespace ec {

FlashProtectCommand::FlashProtectCommand(const uint32_t flags,
                                         const uint32_t mask)
    : EcCommand(EC_CMD_FLASH_PROTECT, EC_VER_FLASH_PROTECT) {
  Req()->flags = flags;
  Req()->mask = mask;
}

/**
 * @return string names of set flags
 */
std::string FlashProtectCommand::ParseFlags(uint32_t flags) {
  std::string output;
  if (flags & EC_FLASH_PROTECT_RO_AT_BOOT) {
    output += "RO_AT_BOOT  ";
  }
  if (flags & EC_FLASH_PROTECT_RO_NOW) {
    output += "RO_NOW  ";
  }
  if (flags & EC_FLASH_PROTECT_ALL_NOW) {
    output += "ALL_NOW  ";
  }
  if (flags & EC_FLASH_PROTECT_GPIO_ASSERTED) {
    output += "GPIO_ASSERTED  ";
  }
  if (flags & EC_FLASH_PROTECT_ERROR_STUCK) {
    output += "ERROR_STUCK  ";
  }
  if (flags & EC_FLASH_PROTECT_ERROR_INCONSISTENT) {
    output += "ERROR_INCONSISTENT  ";
  }
  if (flags & EC_FLASH_PROTECT_ALL_AT_BOOT) {
    output += "ALL_AT_BOOT  ";
  }
  if (flags & EC_FLASH_PROTECT_RW_AT_BOOT) {
    output += "RW_AT_BOOT  ";
  }
  if (flags & EC_FLASH_PROTECT_RW_NOW) {
    output += "RW_NOW  ";
  }
  if (flags & EC_FLASH_PROTECT_ROLLBACK_AT_BOOT) {
    output += "ROLLBACK_AT_BOOT  ";
  }
  if (flags & EC_FLASH_PROTECT_ROLLBACK_NOW) {
    output += "ROLLBACK_NOW  ";
  }

  return output;
}

}  // namespace ec
