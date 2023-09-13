// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_METADATA_COMPRESSOR_INTERFACE_H_
#define DLCSERVICE_METADATA_COMPRESSOR_INTERFACE_H_

#include <memory>
#include <optional>
#include <string>

namespace dlcservice::metadata {

class CompressorInterface {
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
  virtual std::optional<std::string> Process(const std::string& data_in,
                                             bool flush) = 0;

  // Reset the state of the object.
  virtual bool Reset() = 0;
};

}  // namespace dlcservice::metadata

#endif  // DLCSERVICE_METADATA_COMPRESSOR_INTERFACE_H_
