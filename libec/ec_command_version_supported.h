// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_EC_COMMAND_VERSION_SUPPORTED_H_
#define LIBEC_EC_COMMAND_VERSION_SUPPORTED_H_

#include <cstdint>

#include "libec/ec_command.h"

namespace ec {

class EcCommandVersionSupportedInterface {
 public:
  EcCommandVersionSupportedInterface() = default;
  EcCommandVersionSupportedInterface(
      const EcCommandVersionSupportedInterface&) = delete;
  EcCommandVersionSupportedInterface& operator=(
      const EcCommandVersionSupportedInterface&) = delete;

  virtual ~EcCommandVersionSupportedInterface() = default;

  virtual ec::EcCmdVersionSupportStatus EcCmdVersionSupported(uint16_t cmd,
                                                              uint32_t ver) = 0;
};

}  // namespace ec

#endif  // LIBEC_EC_COMMAND_VERSION_SUPPORTED_H_
