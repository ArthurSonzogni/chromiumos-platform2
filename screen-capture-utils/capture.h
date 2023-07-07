// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SCREEN_CAPTURE_UTILS_CAPTURE_H_
#define SCREEN_CAPTURE_UTILS_CAPTURE_H_

#include <stdint.h>

namespace screenshot {

class DisplayBuffer {
 public:
  struct Result {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    void* buffer;
  };
  DisplayBuffer(const DisplayBuffer&) = delete;
  DisplayBuffer& operator=(const DisplayBuffer&) = delete;

  DisplayBuffer() = default;
  virtual ~DisplayBuffer() = default;
  virtual Result Capture(bool rotate) = 0;
};

}  // namespace screenshot

#endif  // SCREEN_CAPTURE_UTILS_CAPTURE_H_
