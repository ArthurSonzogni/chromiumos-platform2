// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/biometrics_auth_block_service.h"

#include <utility>

#include <base/callback.h>
#include <gtest/gtest.h>

#include "cryptohome/auth_blocks/mock_biometrics_command_processor.h"

namespace cryptohome {
namespace {

class BiometricsAuthBlockServiceTest : public ::testing::Test {
 public:
  void SetUp() override {
    auto mock_processor = std::make_unique<MockBiometricsCommandProcessor>();
    mock_processor_ = mock_processor.get();
    service_ = std::make_unique<BiometricsAuthBlockService>(
        std::move(mock_processor), base::DoNothing(), base::DoNothing());
  }

 protected:
  MockBiometricsCommandProcessor* mock_processor_;
  std::unique_ptr<BiometricsAuthBlockService> service_;
};

}  // namespace
}  // namespace cryptohome
