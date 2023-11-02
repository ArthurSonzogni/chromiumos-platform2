//
// Copyright (C) 2015 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "update_engine/metrics_utils.h"

#include <gtest/gtest.h>

namespace chromeos_update_engine {
namespace metrics_utils {

class MetricsUtilsTest : public ::testing::Test {};

TEST(MetricsUtilsTest, GetConnectionType) {
  // Check that expected combinations map to the right value.
  EXPECT_EQ(metrics::ConnectionType::kUnknown,
            GetConnectionType(ConnectionType::kUnknown,
                              /*metered=*/false));
  EXPECT_EQ(metrics::ConnectionType::kDisconnected,
            GetConnectionType(ConnectionType::kDisconnected,
                              /*metered=*/false));
  EXPECT_EQ(metrics::ConnectionType::kEthernet,
            GetConnectionType(ConnectionType::kEthernet,
                              /*metered=*/false));
  EXPECT_EQ(metrics::ConnectionType::kWifi,
            GetConnectionType(ConnectionType::kWifi,
                              /*metered=*/false));
  EXPECT_EQ(metrics::ConnectionType::kUnmeteredCellular,
            GetConnectionType(ConnectionType::kCellular,
                              /*metered=*/false));

  EXPECT_EQ(metrics::ConnectionType::kUnknown,
            GetConnectionType(ConnectionType::kUnknown,
                              /*metered=*/true));
  EXPECT_EQ(metrics::ConnectionType::kDisconnected,
            GetConnectionType(ConnectionType::kDisconnected,
                              /*metered=*/true));
  EXPECT_EQ(metrics::ConnectionType::kEthernet,
            GetConnectionType(ConnectionType::kEthernet,
                              /*metered=*/true));
  EXPECT_EQ(metrics::ConnectionType::kMeteredWifi,
            GetConnectionType(ConnectionType::kWifi,
                              /*metered=*/true));
  EXPECT_EQ(metrics::ConnectionType::kCellular,
            GetConnectionType(ConnectionType::kCellular,
                              /*metered=*/true));
}

}  // namespace metrics_utils
}  // namespace chromeos_update_engine
