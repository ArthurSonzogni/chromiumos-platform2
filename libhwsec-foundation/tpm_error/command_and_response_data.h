// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FOUNDATION_TPM_ERROR_COMMAND_AND_RESPONSE_DATA_H_
#define LIBHWSEC_FOUNDATION_TPM_ERROR_COMMAND_AND_RESPONSE_DATA_H_

#include <stdint.h>
#include <optional>

#include "libhwsec-foundation/hwsec-foundation_export.h"

namespace hwsec_foundation {

struct CommandAndResponseData {
  enum class CommandType {
    kGeneric = 0,
    kGscExtension = 1,
    kGscVendor = 2,
  };
  CommandType command_type;
  // TPM command or vendor subcommand code.
  uint32_t command;
  // TPM response.
  uint32_t response;
};

std::optional<uint32_t> EncodeCommandAndResponse(
    const CommandAndResponseData& data);
std::optional<CommandAndResponseData> DecodeCommandAndResponse(uint32_t value);

}  // namespace hwsec_foundation

#endif  // LIBHWSEC_FOUNDATION_TPM_ERROR_COMMAND_AND_RESPONSE_DATA_H_
