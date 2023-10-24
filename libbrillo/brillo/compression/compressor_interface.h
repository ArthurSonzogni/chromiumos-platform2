// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_COMPRESSION_COMPRESSOR_INTERFACE_H_
#define LIBBRILLO_BRILLO_COMPRESSION_COMPRESSOR_INTERFACE_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "brillo/brillo_export.h"

namespace brillo {

// An interface providing shared functionality between compressors and
// decompressors such as initializing, resetting, and processing data.
class BRILLO_EXPORT CompressorInterface {
 public:
  CompressorInterface() = default;
  virtual ~CompressorInterface() = default;

  CompressorInterface(const CompressorInterface&) = delete;
  CompressorInterface& operator=(const CompressorInterface&) = delete;

  // Initialize the object.
  virtual bool Initialize() = 0;

  // Make a deep copy, returns nullptr on failure.
  virtual std::unique_ptr<CompressorInterface> Clone() = 0;

  // Process the input data with the best possible (de)compression ratio. If
  // `flush` is not requested, this method returns the output string available
  // at the moment and keeps the (de)compression state so that succeeding input
  // will be treated like in the same stream. Otherwise, all the input data will
  // be processed and flushed to the output, and ends the current stream. if a
  // critical error occurs, it resets the state and returns nullopt.
  //
  // While Process() can be called multiple times with `flush` = false to do
  // partial processing (eg. if the data is too large to fit into memory), but
  // the last call (and only the last call) needs `flush` = true.
  virtual std::optional<std::vector<uint8_t>> Process(
      const std::vector<uint8_t>& data_in, bool flush) = 0;

  // Reset the state of the object.
  virtual bool Reset() = 0;
};

}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_COMPRESSION_COMPRESSOR_INTERFACE_H_
