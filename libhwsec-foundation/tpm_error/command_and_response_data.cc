// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec-foundation/tpm_error/command_and_response_data.h"

#include <stdint.h>
#include <optional>

namespace hwsec_foundation {

std::optional<CommandAndResponseData::CommandType> ToCommandType(
    uint32_t value) {
  switch (value) {
    case 0:
      return CommandAndResponseData::CommandType::kGeneric;
    case 1:
      return CommandAndResponseData::CommandType::kGscExtension;
    case 2:
      return CommandAndResponseData::CommandType::kGscVendor;
  }
  return std::nullopt;
}

std::optional<uint32_t> EncodeCommandAndResponse(
    const CommandAndResponseData& data) {
  uint32_t command_type = static_cast<uint32_t>(data.command_type);
  if (command_type > 0xF || data.command > 0x0FFF || data.response > 0xFFFF) {
    return std::nullopt;
  }
  return (command_type << 28) + (data.command << 16) + data.response;
}

std::optional<CommandAndResponseData> DecodeCommandAndResponse(uint32_t value) {
  auto command_type = ToCommandType(value >> 28);
  if (!command_type.has_value()) {
    return std::nullopt;
  }
  return CommandAndResponseData{
      .command_type = *command_type,
      .command = (value >> 16) & 0x0FFF,
      .response = value & 0xFFFF,
  };
}

}  // namespace hwsec_foundation
