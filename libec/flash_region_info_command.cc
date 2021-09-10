// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/flash_region_info_command.h"

namespace ec {

FlashRegionInfoCommand::FlashRegionInfoCommand(enum ec_flash_region region)
    : EcCommand(EC_CMD_FLASH_REGION_INFO, 1) {
  Req()->region = region;
}

uint32_t FlashRegionInfoCommand::GetOffset() const {
  return Resp()->offset;
}

uint32_t FlashRegionInfoCommand::GetSize() const {
  return Resp()->size;
}

}  // namespace ec
