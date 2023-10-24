// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_COMPRESSION_MOCK_COMPRESSOR_H_
#define LIBBRILLO_BRILLO_COMPRESSION_MOCK_COMPRESSOR_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gmock/gmock.h>

#include "brillo/compression/compressor_interface.h"

namespace brillo {

class MockCompressor : public CompressorInterface {
 public:
  MockCompressor() = default;

  MockCompressor(const MockCompressor&) = delete;
  MockCompressor& operator=(const MockCompressor&) = delete;

  MOCK_METHOD(bool, Initialize, (), (override));
  MOCK_METHOD(std::unique_ptr<CompressorInterface>, Clone, (), (override));
  MOCK_METHOD(std::optional<std::vector<uint8_t>>,
              Process,
              (const std::vector<uint8_t>&, bool),
              (override));
  MOCK_METHOD(bool, Reset, (), (override));
};

}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_COMPRESSION_MOCK_COMPRESSOR_H_
