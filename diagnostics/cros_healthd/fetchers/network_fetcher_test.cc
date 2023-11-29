// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/network_fetcher.h"

#include <utility>

#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/system/fake_mojo_service.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/cros_healthd/utils/mojo_task_environment.h"
#include "diagnostics/mojom/external/network_health_types.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace {

class NetworkFetcherTest : public testing::Test {
 protected:
  void SetUp() override {
    mock_context_.fake_mojo_service()->InitializeFakeMojoService();
  }

  ash::cros_healthd::mojom::NetworkResultPtr FetchNetworkInfoSync() {
    base::test::TestFuture<ash::cros_healthd::mojom::NetworkResultPtr> future;
    FetchNetworkInfo(&mock_context_, future.GetCallback());
    return future.Take();
  }

  FakeNetworkHealthService& fake_network_health_service() {
    return mock_context_.fake_mojo_service()->fake_network_health_service();
  }

  void ResetNetworkHealthService() {
    mock_context_.fake_mojo_service()->ResetNetworkHealthService();
  }

 private:
  MojoTaskEnvironment env_;
  MockContext mock_context_;
};

// Test an appropriate error is returned if no remote is bound;
TEST_F(NetworkFetcherTest, NoRemote) {
  ResetNetworkHealthService();
  auto result = FetchNetworkInfoSync();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type,
            ash::cros_healthd::mojom::ErrorType::kServiceUnavailable);
}

// Test that if the remote is bound, the NetworkHealthState is returned.
TEST_F(NetworkFetcherTest, GetNetworkHealthState) {
  auto network = chromeos::network_health::mojom::Network::New();
  network->name = "My WiFi";
  network->type = chromeos::network_config::mojom::NetworkType::kWiFi;
  network->state = chromeos::network_health::mojom::NetworkState::kOnline;
  network->signal_strength =
      chromeos::network_health::mojom::UInt32Value::New(70);
  network->mac_address = "00:11:22:33:44:55";

  auto network_health_state =
      chromeos::network_health::mojom::NetworkHealthState::New();
  network_health_state->networks.push_back(network.Clone());

  fake_network_health_service().SetHealthSnapshotResponse(
      std::move(network_health_state));
  auto result = FetchNetworkInfoSync();
  ASSERT_TRUE(result->is_network_health());
  EXPECT_EQ(result->get_network_health()->networks.size(), 1);
  EXPECT_EQ(result->get_network_health()->networks[0], network);
}

}  // namespace
}  // namespace diagnostics
