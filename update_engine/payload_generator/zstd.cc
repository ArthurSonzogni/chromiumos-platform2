// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/payload_generator/zstd.h"

#include <stdlib.h>
#include <zstd.h>

#include "update_engine/common/utils.h"

namespace chromeos_update_engine {

namespace {

bool ZstdCompressWithOptions(const brillo::Blob& in,
                             brillo::Blob* out,
                             bool window_log) {
  TEST_AND_RETURN_FALSE(out);
  out->clear();
  if (in.size() == 0)
    return true;

  out->resize(in.size());

  // Options step.
  ZSTD_CCtx* ctx = ZSTD_createCCtx();

  if (ZSTD_isError(
          ZSTD_CCtx_setParameter(ctx, ZSTD_c_enableLongDistanceMatching, 1)) ||
      ZSTD_isError(ZSTD_CCtx_setParameter(ctx, ZSTD_c_compressionLevel, 22))) {
    ZSTD_freeCCtx(ctx);
    return false;
  }
  if (window_log && ZSTD_isError(ZSTD_CCtx_setParameter(
                        ctx, ZSTD_c_windowLog, ZSTD_WINDOWLOG_MAX_32))) {
    ZSTD_freeCCtx(ctx);
    return false;
  }

  // Compression step.
  size_t size = ZSTD_compress2(ctx,
                               // Destination.
                               out->data(), out->size(),
                               // Source.
                               in.data(), in.size());
  ZSTD_freeCCtx(ctx);

  // Handle error / result step.
  if (ZSTD_isError(size))
    return false;
  out->resize(size);
  return true;
}

}  // namespace

bool ZstdCompress(const brillo::Blob& in, brillo::Blob* out) {
  return ZstdCompressWithOptions(in, out, /*window_log=*/false);
}

bool ZstdCompressIncreasedWindow(const brillo::Blob& in, brillo::Blob* out) {
  return ZstdCompressWithOptions(in, out, /*window_log=*/true);
}

}  // namespace chromeos_update_engine
