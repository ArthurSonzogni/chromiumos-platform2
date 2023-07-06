// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <vector>

#include "screen-capture-utils/egl_capture.h"
#include "screen-capture-utils/kmsvnc_utils.h"

namespace screenshot {

namespace {
bool RunConvertBuffer(uint32_t crtc_width, uint32_t crtc_height) {
  uint32_t stride = crtc_width * kBytesPerPixel;

  uint32_t vnc_width = GetVncWidth(crtc_width);
  uint32_t vnc_height = crtc_height;

  // Display buffer initialized with dummy val of 0xAABBCCDD;
  uint32_t dummy_value = 0xAABBCCDD;

  std::vector<uint32_t> display_buffer(crtc_width * crtc_height, dummy_value);
  std::vector<uint32_t> vnc_buffer(vnc_width * vnc_height);

  DisplayBuffer::Result display{crtc_width, crtc_height, stride,
                                reinterpret_cast<char*>(display_buffer.data())};

  ConvertBuffer(display, vnc_buffer.data(), vnc_width);

  int index = 0;
  bool buffer_matched = true;
  int pad_index = crtc_width;

  for (uint32_t v : vnc_buffer) {
    int display_index = index % vnc_width;
    if (display_index < pad_index) {
      if (v != dummy_value) {
        buffer_matched = false;
        break;
      }
    } else {
      if (v != 0) {
        buffer_matched = false;
        break;
      }
    }
    index++;
  }
  return buffer_matched;
}
}  // namespace

TEST(VncServerTest, HandlesPadding) {
  EXPECT_EQ(GetVncWidth(5), 8);
  EXPECT_EQ(GetVncWidth(12), 12);
}

TEST(VncServerTest, ConvertBuffer) {
  // Given: A display (W x H)
  // When: Convert display buffer to VNC Buffer where width is a mult of 4
  // Then: VNC Buffer contains display buffer data, but right padded with 0
  //       if display width is not a multiple of 4
  EXPECT_TRUE(RunConvertBuffer(40, 2));
  EXPECT_TRUE(RunConvertBuffer(1366, 768));  // width not a mult of 4
}

}  // namespace screenshot
