// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fingerprint/fp_preload_template_command.h"

#include <algorithm>
#include <vector>

#include <base/strings/string_number_conversions.h>

namespace ec {

bool FpPreloadTemplateCommand::Run(int fd) {
  uint32_t max_data_chunk =
      max_write_size_ - sizeof(fp_preload_template::Header);

  auto pos = template_data_.begin();
  const auto end = template_data_.cend();
  do {
    uint32_t remaining = end - pos;
    uint32_t transfer_len = std::min(max_data_chunk, remaining);
    Req()->req.offset = pos - template_data_.begin();
    Req()->req.size =
        transfer_len | (remaining == transfer_len ? FP_TEMPLATE_COMMIT : 0);
    Req()->req.fgr = finger_;
    std::copy(pos, pos + transfer_len, Req()->data.begin());
    SetReqSize(transfer_len + sizeof(fp_preload_template::Header));
    if (!EcCommandRun(fd)) {
      LOG(ERROR) << "Failed to run FP_PRELOAD_TEMPLATE command";
      return false;
    }
    if (Result() != EC_RES_SUCCESS) {
      LOG(ERROR) << "FP_PRELOAD_TEMPLATE command failed @ "
                 << pos - template_data_.begin();
      return false;
    }
    pos += transfer_len;
  } while (pos < end);
  return true;
}

bool FpPreloadTemplateCommand::EcCommandRun(int fd) {
  return EcCommand::Run(fd);
}

}  // namespace ec
