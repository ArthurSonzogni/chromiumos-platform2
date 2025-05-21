// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hardware_verifier/factory_hwid_processor_impl.h"

#include <memory>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "hardware_verifier/encoding_spec_loader.h"
#include "hardware_verifier/test_utils.h"

namespace hardware_verifier {
namespace {

using ::testing::_;
using ::testing::ByMove;
using ::testing::NiceMock;
using ::testing::Return;

class MockEncodingSpecLoader : public EncodingSpecLoader {
 public:
  MOCK_METHOD(std::unique_ptr<EncodingSpec>, Load, (), (const, override));
};

class FactoryHWIDProcessorImplTest : public BaseFileTest {};

TEST_F(FactoryHWIDProcessorImplTest, Create_LoadSuccess) {
  auto dummy_spec = std::make_unique<EncodingSpec>();
  NiceMock<MockEncodingSpecLoader> mock_loader;
  EXPECT_CALL(mock_loader, Load())
      .WillOnce(Return(ByMove(std::move(dummy_spec))));

  auto processor = FactoryHWIDProcessorImpl::Create(mock_loader);

  EXPECT_NE(processor, nullptr);
}

TEST_F(FactoryHWIDProcessorImplTest, Create_LoadFails) {
  NiceMock<MockEncodingSpecLoader> mock_loader;
  EXPECT_CALL(mock_loader, Load()).WillOnce(Return(nullptr));

  auto processor = FactoryHWIDProcessorImpl::Create(mock_loader);

  EXPECT_EQ(processor, nullptr);
}

}  // namespace
}  // namespace hardware_verifier
