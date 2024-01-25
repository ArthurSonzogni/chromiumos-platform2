// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <string.h>

#include "hwdrm-videoproc-ta/h264_parser.h"

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // We use the first chunk of data for the StreamDataForSliceHeader and then
  // the remaining for the slice header itself.
  struct StreamDataForSliceHeader stream_data;
  struct H264SliceHeaderData hdr_out;
  if (size < sizeof(stream_data))
    return 0;
  memcpy(&stream_data, data, sizeof(stream_data));
  ParseSliceHeader(data + sizeof(stream_data), size - sizeof(stream_data),
                   &stream_data, &hdr_out);
  return 0;
}
