// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/flash_write_command.h"

#include <algorithm>
#include <vector>

namespace ec {

bool FlashWriteCommand::Run(int fd) {
  uint32_t max_data_chunk = max_packet_size_ - sizeof(flash_write::Header);

  auto pos = write_data_.begin();
  while (pos < write_data_.end()) {
    uint32_t remaining = write_data_.end() - pos;
    uint32_t transfer_len = std::min(max_data_chunk, remaining);
    Req()->req.offset = pos - write_data_.begin() + offset_;
    Req()->req.size = transfer_len;
    std::copy(pos, pos + transfer_len, Req()->data.begin());
    SetReqSize(transfer_len + sizeof(flash_write::Header));
    if (!EcCommandRun(fd)) {
      return false;
    }
    if (Result() != EC_RES_SUCCESS) {
      LOG(ERROR) << "FLASH_WRITE command failed @ "
                 << pos - write_data_.begin();
      return false;
    }
    pos += transfer_len;
  }
  return true;
}

bool FlashWriteCommand::EcCommandRun(int fd) {
  return EcCommand::Run(fd);
}

}  // namespace ec
