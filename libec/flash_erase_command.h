// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FLASH_ERASE_COMMAND_H_
#define LIBEC_FLASH_ERASE_COMMAND_H_

#include <memory>
#include <string>

#include "libec/ec_command.h"
#include "libec/ec_command_async.h"
#include "libec/flash_erase_params.h"

namespace ec {

class FlashEraseCommand_v0
    : public EcCommand<struct ec_params_flash_erase, EmptyParam> {
 public:
  ~FlashEraseCommand_v0() override = default;
  static std::unique_ptr<FlashEraseCommand_v0> Create(uint32_t offset,
                                                      uint32_t size);

 private:
  FlashEraseCommand_v0(uint32_t offset, uint32_t size);
};

class FlashEraseCommand_v1
    : public EcCommandAsync<flash_erase::Params_v1, EmptyParam> {
 public:
  ~FlashEraseCommand_v1() override = default;
  static std::unique_ptr<FlashEraseCommand_v1> Create(uint32_t offset,
                                                      uint32_t size);

 private:
  using Options = EcCommandAsync<flash_erase::Params_v1, EmptyParam>::Options;
  FlashEraseCommand_v1(enum ec_flash_erase_cmd cmd,
                       uint32_t offset,
                       uint32_t size);
};

}  // namespace ec

#endif  // LIBEC_FLASH_ERASE_COMMAND_H_
