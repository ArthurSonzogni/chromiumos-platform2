// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "builder.h"

#include <vector>

#include "frame.h"
#include "ipp_frame_builder.h"

namespace ipp {

size_t CalculateLengthOfBinaryFrame(const Frame& frame) {
  std::vector<GroupAsTNVs> tnvs = PreprocessFrame(frame);
  return GetFrameLength(frame, tnvs);
}

size_t BuildBinaryFrame(const Frame& frame,
                        uint8_t* buffer,
                        size_t buffer_length) {
  std::vector<GroupAsTNVs> tnvs = PreprocessFrame(frame);
  const size_t length = GetFrameLength(frame, tnvs);
  if (length > buffer_length) {
    return 0;
  }
  WriteFrameToBuffer(frame, tnvs, buffer);
  return length;
}

std::vector<uint8_t> BuildBinaryFrame(const Frame& frame) {
  std::vector<GroupAsTNVs> tnvs = PreprocessFrame(frame);
  std::vector<uint8_t> buffer(GetFrameLength(frame, tnvs));
  WriteFrameToBuffer(frame, tnvs, buffer.data());
  return buffer;
}

}  // namespace ipp
