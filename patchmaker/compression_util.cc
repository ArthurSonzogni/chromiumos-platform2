// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchmaker/compression_util.h"

#include <optional>

#include <base/files/file_path.h>
#include <base/files/file_util.h>

#include "patchmaker/file_util.h"

// We do not want to perform the most aggressive compression here for
// performance reasons. This utility is used to perform a quick
// comparison to understand the effectiveness of compression, and for
// this purpose an intermediate compression level is effective.
#define ZSTD_COMPRESSION_LEVEL_TEST 5

namespace util {

bool Compress(const brillo::Blob& in, brillo::Blob* out) {
  ScopedZstdCtx ctx(ZSTD_createCCtx());
  size_t compressed_size;

  if (ZSTD_isError(ZSTD_CCtx_setParameter(ctx.get(), ZSTD_c_compressionLevel,
                                          ZSTD_COMPRESSION_LEVEL_TEST))) {
    return false;
  }

  // Ensure our buffer has enough space to hold compressed contents, as in
  // theory it could be larger than the input file.
  out->resize(ZSTD_compressBound(in.size()));

  // Compression step.
  compressed_size = ZSTD_compress2(ctx.get(),
                                   // Destination.
                                   out->data(), out->size(),
                                   // Source.
                                   in.data(), in.size());
  out->resize(compressed_size);

  if (ZSTD_isError(compressed_size))
    return false;

  return true;
}

bool Decompress(const brillo::Blob& in, brillo::Blob* out) {
  out->resize(ZSTD_getFrameContentSize(in.data(), in.size()));

  // Compression step.
  size_t decompressed_size = ZSTD_decompress(
      // Destination.
      out->data(), out->size(),
      // Source.
      in.data(), in.size());
  out->resize(decompressed_size);

  // Clean up
  if (ZSTD_isError(decompressed_size))
    return false;

  return true;
}

std::optional<size_t> GetCompressedSize(const base::FilePath& path) {
  std::optional<brillo::Blob> src;

  src = ReadFileToBlob(path);
  if (!src.has_value()) {
    LOG(ERROR) << "Failed to load file into blob";
    return std::nullopt;
  }

  brillo::Blob dst;
  if (!Compress(*src, &dst)) {
    LOG(ERROR) << "Compress call failed";
    return std::nullopt;
  }

  return dst.size();
}

}  // namespace util
