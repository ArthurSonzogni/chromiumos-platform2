// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_METADATA_ZLIB_COMPRESSOR_H_
#define DLCSERVICE_METADATA_ZLIB_COMPRESSOR_H_

#include <zlib.h>

#include <memory>
#include <optional>
#include <string>

#include <brillo/brillo_export.h>

#include "dlcservice/metadata/compressor_interface.h"

namespace dlcservice::metadata {

class BRILLO_EXPORT ZlibCompressor : public CompressorInterface {
 public:
  ZlibCompressor();
  ~ZlibCompressor() override;

  ZlibCompressor(const ZlibCompressor&) = delete;
  ZlibCompressor& operator=(const ZlibCompressor&) = delete;

  bool Initialize() override;
  std::unique_ptr<CompressorInterface> Clone() override;
  std::optional<std::string> Process(const std::string& data_in,
                                     bool flush) override;
  bool Reset() override;

 private:
  z_stream zstream_;
};

class BRILLO_EXPORT ZlibDecompressor : public CompressorInterface {
 public:
  ZlibDecompressor();
  ~ZlibDecompressor() override;

  ZlibDecompressor(const ZlibDecompressor&) = delete;
  ZlibDecompressor& operator=(const ZlibDecompressor&) = delete;

  bool Initialize() override;
  std::unique_ptr<CompressorInterface> Clone() override;
  std::optional<std::string> Process(const std::string& data_in,
                                     bool flush) override;
  bool Reset() override;

 private:
  z_stream zstream_;
};

}  // namespace dlcservice::metadata

#endif  // DLCSERVICE_METADATA_ZLIB_COMPRESSOR_H_
