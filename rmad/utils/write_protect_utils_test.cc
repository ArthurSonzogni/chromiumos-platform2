// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/write_protect_utils_impl.h"

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/utils/mock_crossystem_utils.h"
#include "rmad/utils/mock_ec_utils.h"
#include "rmad/utils/mock_flashrom_utils.h"

using testing::NiceMock;

namespace rmad {

class WriteProtectUtilsTest : public testing::Test {
 public:
  WriteProtectUtilsTest() = default;
  ~WriteProtectUtilsTest() override = default;

  std::unique_ptr<WriteProtectUtilsImpl> CreateWriteProtectUtils() {
    auto mock_crossystem_utils =
        std::make_unique<NiceMock<MockCrosSystemUtils>>();
    auto mock_ec_utils = std::make_unique<NiceMock<MockEcUtils>>();
    auto mock_flashrom_utils = std::make_unique<NiceMock<MockFlashromUtils>>();

    return std::make_unique<WriteProtectUtilsImpl>(
        std::move(mock_crossystem_utils), std::move(mock_ec_utils),
        std::move(mock_flashrom_utils));
  }
};

}  // namespace rmad
