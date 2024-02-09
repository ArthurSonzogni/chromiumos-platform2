// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/payload_generator/xz.h"

#include <base/logging.h>
#include <lzma.h>

namespace chromeos_update_engine {

void XzCompressInit() {}

bool XzCompress(const brillo::Blob& in, brillo::Blob* out) {
  out->clear();
  if (in.empty())
    return true;

  // Resize the output buffer to get enough memory for writing the compressed
  // data.
  out->resize(lzma_stream_buffer_bound(in.size()));

  const uint32_t kLzmaPreset = 6;
  size_t out_pos = 0;
  int rc = lzma_easy_buffer_encode(kLzmaPreset,
                                   LZMA_CHECK_NONE,  // We do not need CRC.
                                   nullptr, in.data(), in.size(), out->data(),
                                   &out_pos, out->size());
  if (rc != LZMA_OK) {
    LOG(ERROR) << "Failed to compress data to LZMA stream with return code: "
               << rc;
    return false;
  }
  out->resize(out_pos);
  return true;
}

}  // namespace chromeos_update_engine
