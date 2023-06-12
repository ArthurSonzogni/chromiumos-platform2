// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_THERMAL_GET_MEMMAP_THERMAL_VERSION_COMMAND_H_
#define LIBEC_THERMAL_GET_MEMMAP_THERMAL_VERSION_COMMAND_H_

#include <brillo/brillo_export.h>

#include <cstdint>
#include <string>

#include "libec/ec_command.h"
#include "libec/read_memmap_command.h"

namespace ec {

class BRILLO_EXPORT GetMemmapThermalVersionCommand
    : public ReadMemmapMem8Command {
 public:
  GetMemmapThermalVersionCommand()
      : ReadMemmapMem8Command(EC_MEMMAP_THERMAL_VERSION) {}
  ~GetMemmapThermalVersionCommand() override = default;

  std::optional<uint8_t> ThermalVersion() const {
    if (!Resp())
      return std::nullopt;
    return *Resp();
  }

  bool Run(int fd) override { return ReadMemmapMem8Command::Run(fd); };
};

static_assert(
    !std::is_copy_constructible<GetMemmapThermalVersionCommand>::value,
    "EcCommands are not copyable by default");
static_assert(!std::is_copy_assignable<GetMemmapThermalVersionCommand>::value,
              "EcCommands are not copy-assignable by default");

}  // namespace ec

#endif  // LIBEC_THERMAL_GET_MEMMAP_THERMAL_VERSION_COMMAND_H_
