// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
