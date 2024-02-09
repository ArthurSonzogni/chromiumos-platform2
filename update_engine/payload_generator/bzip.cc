// Copyright 2010 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/payload_generator/bzip.h"

#include <bzlib.h>
#include <stdlib.h>

#include <algorithm>
#include <limits>

#include "update_engine/common/utils.h"

namespace chromeos_update_engine {

bool BzipCompress(const brillo::Blob& in, brillo::Blob* out) {
  TEST_AND_RETURN_FALSE(out);
  out->clear();
  if (in.size() == 0)
    return true;

  // We expect a compression ratio of about 35% with bzip2, so we start with
  // that much output space, which will then be doubled if needed.
  size_t buf_size = 40 + in.size() * 35 / 100;
  out->resize(buf_size);

  // Try increasing buffer size until it works
  for (;;) {
    if (buf_size > std::numeric_limits<uint32_t>::max())
      return false;
    uint32_t data_size = buf_size;
    int rc = BZ2_bzBuffToBuffCompress(
        reinterpret_cast<char*>(out->data()), &data_size,
        reinterpret_cast<char*>(const_cast<uint8_t*>(in.data())), in.size(),
        9,   // Best compression
        0,   // Silent verbosity
        0);  // Default work factor
    TEST_AND_RETURN_FALSE(rc == BZ_OUTBUFF_FULL || rc == BZ_OK);
    if (rc == BZ_OK) {
      // we're done!
      out->resize(data_size);
      return true;
    }

    // Data didn't fit; double the buffer size.
    buf_size *= 2;
    out->resize(buf_size);
  }
}

}  // namespace chromeos_update_engine
