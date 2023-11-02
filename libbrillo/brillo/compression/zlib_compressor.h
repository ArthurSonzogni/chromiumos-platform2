// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_COMPRESSION_ZLIB_COMPRESSOR_H_
#define LIBBRILLO_BRILLO_COMPRESSION_ZLIB_COMPRESSOR_H_

#include <zconf.h>
#include <zlib.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "brillo/brillo_export.h"
#include "brillo/compression/compressor_interface.h"

namespace brillo {

// Window bit values taken from https://www.zlib.net/manual.html. See
// deflateInit2() and inflateInit2().
inline constexpr int kGzipFormatWbits = 16;
inline constexpr int kZlibOrGzipFormatWbits = 32;

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
  enum class DeflateFormat {
    // Generates a simple zlib header and trailer around the compressed data.
    Zlib = MAX_WBITS,
    // Generates a simple gzip header and trailer around the compressed data.
    // The gzip header will have no file name, no extra data, no comment, no
    // modification time (set to zero), no header crc, and the operating system
    // will be set to the appropriate value, if the operating system was
    // determined at compile time.
    Gzip = MAX_WBITS + kGzipFormatWbits,
    // Generates raw deflate data with no zlib header or trailer, and will not
    // compute a check value.
    Raw = -MAX_WBITS,
  };

  explicit ZlibCompressor(DeflateFormat window_bits);
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
  std::optional<std::vector<uint8_t>> Process(
      const std::vector<uint8_t>& data_in, bool flush) override;

  // Reset the state of the object, returns false on failure.
  bool Reset() override;

 private:
  z_stream zstream_;

  // The base two logarithm of the maximum window size (the size of the history
  // buffer). This value determines how the output is formatted.
  //
  // Initialized to process raw data by default. The intention is that the
  // member variable is created with a valid value so no undefined behavior
  // occurs (eg. if someone makes a new constructor and forgets to initialize
  // this variable).
  DeflateFormat window_bits_ = DeflateFormat::Raw;
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
  enum class InflateFormat {
    // Decodes only zlib compressed data.
    Zlib = MAX_WBITS,
    // Processes raw compressed data, not looking for a zlib or gzip header, not
    // generating a check value, and not looking for any check values for
    // comparison at the end of the stream. This is for use with other formats
    // that use the deflate compressed data format such as zip.
    Raw = -MAX_WBITS,
    // Decodes only the gzip compressed data.
    Gzip = MAX_WBITS + kGzipFormatWbits,
    // Enables decoding zlib and gzip compressed data by automatic header
    // detection.
    ZlibOrGzip = MAX_WBITS + kZlibOrGzipFormatWbits,
  };

  explicit ZlibDecompressor(InflateFormat window_bits);
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
  std::optional<std::vector<uint8_t>> Process(
      const std::vector<uint8_t>& data_in, bool flush) override;

  // Reset the state of the object, returns false on failure.
  bool Reset() override;

 private:
  z_stream zstream_;

  // The base two logarithm of the maximum window size (the size of the history
  // buffer). This value is used to determine how the input is formatted.
  //
  // Initialized to process raw data by default. The intention is that the
  // member variable is created with a valid value so no undefined behavior
  // occurs (eg. if someone makes a new constructor and forgets to initialize
  // this variable).
  InflateFormat window_bits_ = InflateFormat::Raw;
};

}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_COMPRESSION_ZLIB_COMPRESSOR_H_
