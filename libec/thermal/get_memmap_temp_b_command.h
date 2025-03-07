// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_THERMAL_GET_MEMMAP_TEMP_B_COMMAND_H_
#define LIBEC_THERMAL_GET_MEMMAP_TEMP_B_COMMAND_H_

#include <cstdint>
#include <string>

#include <brillo/brillo_export.h>

#include "libec/ec_command.h"
#include "libec/read_memmap_command.h"

namespace ec {

class BRILLO_EXPORT GetMemmapTempBCommand : public ReadMemmapMem8Command {
 public:
  explicit GetMemmapTempBCommand(uint8_t id)
      : ReadMemmapMem8Command(EC_MEMMAP_TEMP_SENSOR_B + id -
                              EC_TEMP_SENSOR_ENTRIES) {}
  ~GetMemmapTempBCommand() override = default;

  std::optional<uint8_t> Temp() const {
    if (!Resp()) {
      return std::nullopt;
    }
    return *Resp();
  }

  bool Run(int fd) override { return ReadMemmapMem8Command::Run(fd); };
};

static_assert(!std::is_copy_constructible<GetMemmapTempBCommand>::value,
              "EcCommands are not copyable by default");
static_assert(!std::is_copy_assignable<GetMemmapTempBCommand>::value,
              "EcCommands are not copy-assignable by default");

}  // namespace ec

#endif  // LIBEC_THERMAL_GET_MEMMAP_TEMP_B_COMMAND_H_
