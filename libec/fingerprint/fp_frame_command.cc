// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fingerprint/fp_frame_command.h"

#include <stdint.h>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/containers/span.h>
#include <base/threading/platform_thread.h>

namespace ec {

bool FpFrameCommand::Run(int fd) {
  uint32_t offset = frame_index_ << FP_FRAME_INDEX_SHIFT;
  auto pos = frame_data_->begin();
  while (pos < frame_data_->end()) {
    // Compare as uint32_t to avoid overflow, then cast to uint16_t since the
    // min value will always fit in uint16_t.
    uint16_t len = static_cast<uint16_t>(
        std::min<uint32_t>(max_read_size_, frame_data_->end() - pos));
    SetReq({.offset = offset, .size = len});
    SetRespSize(len);
    int retries = 0;
    while (!EcCommandRun(fd)) {
      if (!(offset & FP_FRAME_OFFSET_MASK)) {
        // On the first request, the EC might still be rate-limiting. Retry in
        // that case.
        if (Result() == EC_RES_BUSY && retries < kMaxRetries) {
          retries++;
          LOG(INFO) << "Retrying FP_FRAME, attempt " << retries;
          Sleep(base::Milliseconds(kRetryDelayMs));
          continue;
        }
      }
      LOG(ERROR) << "FP_FRAME command failed @ 0x" << std::hex << offset;
      return false;
    }
    std::copy(Resp()->cbegin(), Resp()->cbegin() + len, pos);
    offset += len;
    pos += len;
  }
  return true;
}

std::unique_ptr<std::vector<uint8_t>> FpFrameCommand::frame() {
  return std::move(frame_data_);
}

bool FpFrameCommand::EcCommandRun(int fd) {
  return EcCommand::Run(fd);
}

void FpFrameCommand::Sleep(base::TimeDelta duration) {
  base::PlatformThread::Sleep(duration);
}

std::optional<std::string> FpFrameCommand::FrameToPgm(
    base::span<const uint8_t> frame, const FrameToPgmOptions& options) {
  if (frame.empty()) {
    return std::nullopt;
  }

  uint8_t bytes_per_pixel = std::ceil(options.bpp / 8.0f);
  if (bytes_per_pixel != 1 && bytes_per_pixel != 2) {
    return std::nullopt;
  }

  if (frame.size() < options.width * options.height * bytes_per_pixel) {
    return std::nullopt;
  }

  const uint8_t* ptr = frame.data();

  // Generate 8-bit or 16-bit PGM ASCII output.
  std::stringstream stream;
  stream << "P2\n"
         << options.width << " " << options.height << "\n"
         << ((bytes_per_pixel == 2) ? 65535 : 255) << "\n";

  for (int y = 0; y < options.height; y++) {
    for (int x = 0; x < options.width; x++, ptr += bytes_per_pixel) {
      stream << ((bytes_per_pixel == 2)
                     ? *reinterpret_cast<const uint16_t*>(ptr)
                     : *ptr)
             << " ";
    }
    stream << "\n";
  }
  stream << "# END OF FILE\n";
  return stream.str();
}

}  // namespace ec
