// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <vector>

#include "libec/flash_read_command.h"

namespace ec {

FlashReadCommand::FlashReadCommand(uint32_t offset,
                                   uint32_t read_size,
                                   uint16_t max_packet_size)
    : EcCommand(EC_CMD_FLASH_READ),
      read_data_(read_size),
      offset_(offset),
      max_packet_size_(max_packet_size) {}

std::vector<uint8_t> FlashReadCommand::GetData() const {
  return read_data_;
}

bool FlashReadCommand::Run(int fd) {
  uint32_t max_data_chunk = max_packet_size_;

  auto pos = read_data_.begin();
  while (pos < read_data_.end()) {
    uint32_t remaining = read_data_.end() - pos;
    uint32_t transfer_len = std::min(max_data_chunk, remaining);
    Req()->offset = pos - read_data_.begin() + offset_;
    Req()->size = transfer_len;
    SetRespSize(transfer_len);
    if (!EcCommandRun(fd)) {
      return false;
    }
    if (Result() != EC_RES_SUCCESS) {
      LOG(ERROR) << "FLASH_READ command failed @ " << pos - read_data_.begin();
      return false;
    }
    std::copy(Resp()->cbegin(), Resp()->cbegin() + transfer_len, pos);
    pos += transfer_len;
  }
  return true;
}

bool FlashReadCommand::EcCommandRun(int fd) {
  return EcCommand::Run(fd);
}

}  // namespace ec
