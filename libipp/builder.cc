// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "builder.h"

#include "frame.h"
#include "ipp_frame.h"
#include "ipp_frame_builder.h"

namespace ipp {

size_t CalculateLengthOfBinaryFrame(const Frame& frame) {
  return frame.GetLength();
}

size_t BuildBinaryFrame(const Frame& frame,
                        uint8_t* buffer,
                        size_t buffer_length) {
  return frame.SaveToBuffer(buffer, buffer_length);
}

std::vector<uint8_t> BuildBinaryFrame(const Frame& frame) {
  return frame.SaveToBuffer();
}

}  // namespace ipp
