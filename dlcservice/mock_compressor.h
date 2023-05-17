// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_MOCK_COMPRESSOR_H_
#define DLCSERVICE_MOCK_COMPRESSOR_H_

#include <memory>
#include <optional>
#include <string>

#include <gmock/gmock.h>

#include "dlcservice/compressor_interface.h"

namespace dlcservice {

class MockCompressor : public CompressorInterface {
 public:
  MockCompressor() = default;

  MockCompressor(const MockCompressor&) = delete;
  MockCompressor& operator=(const MockCompressor&) = delete;

  MOCK_METHOD(bool, Initialize, (), (override));
  MOCK_METHOD(std::unique_ptr<CompressorInterface>, Clone, (), (override));
  MOCK_METHOD(std::optional<std::string>,
              Process,
              (const std::string&, bool),
              (override));
  MOCK_METHOD(bool, Reset, (), (override));
};

}  // namespace dlcservice

#endif  // DLCSERVICE_MOCK_COMPRESSOR_H_
