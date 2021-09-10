// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FLASH_REGION_INFO_COMMAND_H_
#define LIBEC_FLASH_REGION_INFO_COMMAND_H_

#include <brillo/brillo_export.h>
#include "libec/ec_command.h"

namespace ec {

class BRILLO_EXPORT FlashRegionInfoCommand
    : public EcCommand<struct ec_params_flash_region_info,
                       struct ec_response_flash_region_info> {
 public:
  explicit FlashRegionInfoCommand(enum ec_flash_region region);
  ~FlashRegionInfoCommand() override = default;

  uint32_t GetOffset() const;
  uint32_t GetSize() const;
};

static_assert(!std::is_copy_constructible<FlashRegionInfoCommand>::value,
              "EcCommands are not copyable by default");
static_assert(!std::is_copy_assignable<FlashRegionInfoCommand>::value,
              "EcCommands are not copy-assignable by default");

}  // namespace ec

#endif  // LIBEC_FLASH_REGION_INFO_COMMAND_H_
