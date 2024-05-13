// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHMAKER_COMPRESSION_UTIL_H_
#define PATCHMAKER_COMPRESSION_UTIL_H_

#include <memory>
#include <zstd.h>

#include <base/files/file_path.h>
#include <brillo/secure_blob.h>

namespace util {

struct ScopedZstdCtxDeleter {
  inline void operator()(ZSTD_CCtx* ctx) const {
    if (ctx) {
      ZSTD_freeCCtx(ctx);
    }
  }
};

using ScopedZstdCtx = std::unique_ptr<ZSTD_CCtx, ScopedZstdCtxDeleter>;

bool Compress(const brillo::Blob& in, brillo::Blob* out);

bool Decompress(const brillo::Blob& in, brillo::Blob* out);

std::optional<size_t> GetCompressedSize(const base::FilePath& path);

}  // namespace util

#endif  // PATCHMAKER_COMPRESSION_UTIL_H_
