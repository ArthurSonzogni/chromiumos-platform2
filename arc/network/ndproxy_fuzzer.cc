// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/network/ndproxy.h"

namespace arc_networkd {

namespace {

constexpr MacAddress guest_if_mac({0xd2, 0x47, 0xf7, 0xc5, 0x9e, 0x53});

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Turn off logging.
  logging::SetMinLogLevel(logging::LOG_FATAL);

  uint8_t* out_buffer_extended = new uint8_t[size + 4];
  uint8_t* out_buffer = NDProxy::AlignFrameBuffer(out_buffer_extended);
  NDProxy ndproxy;
  ndproxy.Init();
  ndproxy.TranslateNDFrame(data, size, guest_if_mac, out_buffer);
  delete[] out_buffer_extended;

  return 0;
}

}  // namespace
}  // namespace arc_networkd
