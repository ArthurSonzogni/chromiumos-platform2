// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/ssfc/ssfc_prober.h"

#include <memory>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/system/mock_runtime_probe_client.h"
#include "rmad/utils/mock_cros_config_utils.h"

using testing::NiceMock;

namespace rmad {

class SsfcProberImplTest : public testing::Test {
 public:
  SsfcProberImplTest() = default;

  std::unique_ptr<SsfcProberImpl> CreateSsfcProber() {
    auto mock_runtime_probe_client =
        std::make_unique<NiceMock<MockRuntimeProbeClient>>();
    auto mock_cros_config_utils =
        std::make_unique<NiceMock<MockCrosConfigUtils>>();
    return std::make_unique<SsfcProberImpl>(
        std::move(mock_runtime_probe_client),
        std::move(mock_cros_config_utils));
  }
};

TEST_F(SsfcProberImplTest, ProbeSSFC) {
  auto ssfc_prober = CreateSsfcProber();

  EXPECT_EQ(0, ssfc_prober->ProbeSSFC());
}

}  // namespace rmad
