// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_COMPRESSION_ZLIB_COMPRESSOR_H_
#define LIBBRILLO_BRILLO_COMPRESSION_ZLIB_COMPRESSOR_H_

#include <zlib.h>

#include <memory>
#include <optional>
#include <string>

#include "brillo/brillo_export.h"
#include "brillo/compression/compressor_interface.h"

namespace brillo {

// ZlibCompressor generates raw compressed data with the best compression
// setting. The current implementation will only generate raw compressed data
// with no zlib header or trailer and will not compute a check value.
// See also https://www.zlib.net/manual.html as a reference for specific zlib
// methods.
// TODO(b/306036605): Add option to generate zlib-compressed and gzip-compressed
// data.
//
// See the following psuedo-code for usage:
//
// std::unique_ptr<CompressorInterface> compressor =
//     std::make_unique<ZlibCompressor>();
// if (!compressor->Initialize()) {
//   LOG(ERROR) << "Failed to initialize compressor";
//   return;  // Do appropriate action for initialization failure.
// }
//
// std::string data_in("Data to compress");
// auto compressed = compressor->Process(data_in, /*flush=*/true);
// if (!compressed.has_value()) {
//   LOG(ERROR) << "Failed to compress data: " << data_in;
// }
class BRILLO_EXPORT ZlibCompressor : public CompressorInterface {
 public:
  ZlibCompressor();
  ~ZlibCompressor() override;

  ZlibCompressor(const ZlibCompressor&) = delete;
  ZlibCompressor& operator=(const ZlibCompressor&) = delete;

  // Initialize the object, returns false on failure.
  bool Initialize() override;

  // Make a deep copy, returns nullptr on failure.
  std::unique_ptr<CompressorInterface> Clone() override;

  // Compress the input data with the best possible compression ratio. If
  // `flush` is not requested, this method returns the output string available
  // at the moment and keeps the compression state so that succeeding input
  // will be treated like in the same stream. Otherwise, all the input data will
  // be processed and flushed to the output, and ends the current stream. if a
  // critical error occurs, it resets the state and returns nullopt.
  //
  // While Process() can be called multiple times with `flush` = false to do
  // partial processing (eg. if the data is too large to fit into memory), but
  // the last call (and only the last call) needs `flush` = true.
  std::optional<std::string> Process(const std::string& data_in,
                                     bool flush) override;

  // Reset the state of the object, returns false on failure.
  bool Reset() override;

 private:
  z_stream zstream_;
};

// ZlibDecompressor decompresses raw compressed data. The current implementation
// will only decompress raw compressed data and will not look for a zlib or gzip
// header, not generate a check value, and not look for any check values for
// comparison at the end of the stream.
// See also https://www.zlib.net/manual.html as a reference for specific zlib
// methods.
// TODO(b/306036605): Add option to decompress zlib-compressed and
// gzip-compressed data.
//
// See ZlibCompressor comments above for similar usage.
class BRILLO_EXPORT ZlibDecompressor : public CompressorInterface {
 public:
  ZlibDecompressor();
  ~ZlibDecompressor() override;

  ZlibDecompressor(const ZlibDecompressor&) = delete;
  ZlibDecompressor& operator=(const ZlibDecompressor&) = delete;

  // Initialize the object, returns false on failure.
  bool Initialize() override;

  // Make a deep copy, returns nullptr on failure.
  std::unique_ptr<CompressorInterface> Clone() override;

  // Decompress the input data with the best possible decompression ratio. If
  // `flush` is not requested, this method returns the output string available
  // at the moment and keeps the decompression state so that succeeding input
  // will be treated like in the same stream. Otherwise, all the input data will
  // be processed and flushed to the output, and ends the current stream. if a
  // critical error occurs, it resets the state and returns nullopt.
  //
  // While Process() can be called multiple times with `flush` = false to do
  // partial processing (eg. if the data is too large to fit into memory), but
  // the last call (and only the last call) needs `flush` = true.
  std::optional<std::string> Process(const std::string& data_in,
                                     bool flush) override;

  // Reset the state of the object, returns false on failure.
  bool Reset() override;

 private:
  z_stream zstream_;
};

}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_COMPRESSION_ZLIB_COMPRESSOR_H_
