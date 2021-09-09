// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

#include <base/memory/ptr_util.h>

#include "libec/ec_command.h"
#include "libec/ec_command_async.h"
#include "libec/flash_erase_command.h"

namespace ec {

constexpr int kFlashSmallRegionThresholdBytes = 16 * 1024;

namespace {

bool OffsetAndSizeAreValid(uint32_t offset, uint32_t size) {
  if (size >
      static_cast<uint64_t>(std::numeric_limits<decltype(offset)>::max()) -
          offset + 1) {
    return false;
  }
  return true;
}

}  // namespace

FlashEraseCommand_v0::FlashEraseCommand_v0(uint32_t offset, uint32_t size)
    : EcCommand(EC_CMD_FLASH_ERASE, 0) {
  Req()->offset = offset;
  Req()->size = size;
}

std::unique_ptr<FlashEraseCommand_v0> FlashEraseCommand_v0::Create(
    uint32_t offset, uint32_t size) {
  if (!OffsetAndSizeAreValid(offset, size)) {
    return nullptr;
  }
  // Using new to access non-public constructor. See https://abseil.io/tips/134.
  return base::WrapUnique(new FlashEraseCommand_v0(offset, size));
}

FlashEraseCommand_v1::FlashEraseCommand_v1(enum ec_flash_erase_cmd cmd,
                                           uint32_t offset,
                                           uint32_t size)
    : EcCommandAsync(EC_CMD_FLASH_ERASE,
                     FLASH_ERASE_GET_RESULT,
                     {.poll_for_result_num_attempts = 20,
                      .poll_interval = base::Milliseconds(500)},
                     1) {
  Req()->action = cmd;
  Req()->params.offset = offset;
  Req()->params.size = size;
}

std::unique_ptr<FlashEraseCommand_v1> FlashEraseCommand_v1::Create(
    uint32_t offset, uint32_t size) {
  if (!OffsetAndSizeAreValid(offset, size)) {
    return nullptr;
  }

  enum ec_flash_erase_cmd cmd = FLASH_ERASE_SECTOR;
  if (size >= kFlashSmallRegionThresholdBytes) {
    cmd = FLASH_ERASE_SECTOR_ASYNC;
  }

  // Using new to access non-public constructor. See https://abseil.io/tips/134.
  return base::WrapUnique(new FlashEraseCommand_v1(cmd, offset, size));
}

}  // namespace ec
