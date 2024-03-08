// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/network_interface_fetcher.h"

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/stringprintf.h>
#include <base/test/gmock_callback_support.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;
using IwCommand = mojom::Executor::IwCommand;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::Return;
using ::testing::WithArg;

constexpr char kFakePowerSchemeContent[] = "2\n";
constexpr char kFakeGetInterfacesOutput[] = "Interface wlan0\n";
constexpr char kFakeGetInterfacesNoWirelessAdapterOutput[] =
    "No wifi adapter found on the system\n";
constexpr char kFakeGetLinkOutput[] =
    "Connected to 11:22:33:44:55:66 (on wlan0)\n"
    "\tsignal: -50 dBm\n"
    "\trx bitrate: 800.0 MBit/s VHT-MCS 9 80MHz short GI VHT-NSS 2\n"
    "\ttx bitrate: 600.0 MBit/s VHT-MCS 7 80MHz VHT-NSS 2\n";
constexpr char kFakeGetLinkOutputWithoutRxBitrate[] =
    "Connected to 11:22:33:44:55:66 (on wlan0)\n"
    "\tsignal: -50 dBm\n"
    "\ttx bitrate: 600.0 MBit/s VHT-MCS 7 80MHz VHT-NSS 2\n";
constexpr char kFakeGetLinkDeviceNotConnectedOutput[] = "Not connected.\n";
constexpr char kFakeGetInfoOutput[] = "txpower 22.00 dBm\n";
constexpr char kFakeGetScanDumpOutput[] =
    "BSS 11:11:11:11:11:11(on wlan0)\n"
    "bss data: some data\n"
    "BSS 11:22:33:44:55:66(on wlan0) -- associated\n"
    "\tlast seen: 1803877.987s [boottime]\n"
    "\tTSF: 4892660736771 usec (56d, 15:04:20)\n"
    "\tfreq: 2462\n"
    "\tbeacon interval: 100 TUs\n"
    "\tcapability: ESS Privacy ShortSlotTime RadioMeasure (0x1431)\n"
    "\tsignal: -82.00 dBm\n";
constexpr char kExpectedInterfaceName[] = "wlan0";
constexpr bool kExpectedPowerManagementOn = true;
constexpr char kExpectedAcessPoint[] = "11:22:33:44:55:66";
constexpr uint32_t kExpectedTxBitRateMbps = 600;
constexpr uint32_t kExpectedRxBitRateMbps = 800;
constexpr uint32_t kExpectedTxPower = 22;
constexpr bool kExpectedEncriptionOn = true;
constexpr int32_t kExpectedLinkQuality = 60;
constexpr int32_t kExpectedSignalLevel = -50;

}  // namespace

class NetworkInterfaceFetcherTest : public ::testing::Test {
 protected:
  NetworkInterfaceFetcherTest() = default;

  void SetUp() override {
    MockReadPowerSchema(kFakePowerSchemeContent);
    MockIw(IwCommand::kDev, "", EXIT_SUCCESS, kFakeGetInterfacesOutput);
    MockIw(IwCommand::kLink, kExpectedInterfaceName, EXIT_SUCCESS,
           kFakeGetLinkOutput);
    MockIw(IwCommand::kInfo, kExpectedInterfaceName, EXIT_SUCCESS,
           kFakeGetInfoOutput);
    MockIw(IwCommand::kScanDump, kExpectedInterfaceName, EXIT_SUCCESS,
           kFakeGetScanDumpOutput);
  }

  MockExecutor* mock_executor() { return mock_context_.mock_executor(); }

  mojom::NetworkInterfaceResultPtr FetchNetworkInterfaceInfoSync() {
    base::test::TestFuture<mojom::NetworkInterfaceResultPtr> future;
    FetchNetworkInterfaceInfo(&mock_context_, future.GetCallback());
    return future.Take();
  }

  void MockIw(IwCommand cmd,
              const std::string& interface_name,
              const int32_t return_code,
              const std::string& output) {
    auto result = mojom::ExecutedProcessResult::New();
    result->return_code = return_code;
    result->out = output;
    EXPECT_CALL(*mock_context_.mock_executor(), RunIw(cmd, interface_name, _))
        .WillRepeatedly(base::test::RunOnceCallback<2>(std::move(result)));
  }

  void MockReadPowerSchema(const std::optional<std::string>& content) {
    EXPECT_CALL(*mock_context_.mock_executor(),
                ReadFile(mojom::Executor::File::kWirelessPowerScheme, _))
        .WillRepeatedly(base::test::RunOnceCallback<1>(content));
  }

 private:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY};
  MockContext mock_context_;
};

// Test TestFetchNetworkInterfaceInfo matching with expected result.
TEST_F(NetworkInterfaceFetcherTest, FetchNetworkInterfaceInfo) {
  MockIw(IwCommand::kDev, "", EXIT_SUCCESS, kFakeGetInterfacesOutput);
  MockIw(IwCommand::kLink, kExpectedInterfaceName, EXIT_SUCCESS,
         kFakeGetLinkOutput);
  MockIw(IwCommand::kInfo, kExpectedInterfaceName, EXIT_SUCCESS,
         kFakeGetInfoOutput);
  MockIw(IwCommand::kScanDump, kExpectedInterfaceName, EXIT_SUCCESS,
         kFakeGetScanDumpOutput);

  auto result = FetchNetworkInterfaceInfoSync();

  ASSERT_TRUE(result->is_network_interface_info());
  const auto& network_infos = result->get_network_interface_info();
  ASSERT_THAT(network_infos, Not(IsEmpty()));
  const auto& network_info = network_infos.at(0);

  ASSERT_FALSE(network_info.is_null());
  switch (network_info->which()) {
    case mojom::NetworkInterfaceInfo::Tag::kWirelessInterfaceInfo: {
      const auto& wireless_info = network_info->get_wireless_interface_info();
      ASSERT_FALSE(wireless_info.is_null());
      EXPECT_EQ(wireless_info->interface_name, kExpectedInterfaceName);
      EXPECT_EQ(wireless_info->power_management_on, kExpectedPowerManagementOn);
      const auto& link_info = wireless_info->wireless_link_info;
      ASSERT_FALSE(link_info.is_null());
      EXPECT_EQ(link_info->access_point_address_str, kExpectedAcessPoint);
      EXPECT_EQ(link_info->tx_bit_rate_mbps, kExpectedTxBitRateMbps);
      EXPECT_EQ(link_info->rx_bit_rate_mbps, kExpectedRxBitRateMbps);
      EXPECT_EQ(link_info->tx_power_dBm, kExpectedTxPower);
      EXPECT_EQ(link_info->encyption_on, kExpectedEncriptionOn);
      EXPECT_EQ(link_info->link_quality, kExpectedLinkQuality);
      EXPECT_EQ(link_info->signal_level_dBm, kExpectedSignalLevel);
      break;
    }
  }
}

// Test case: GetInterfaces return failure.
TEST_F(NetworkInterfaceFetcherTest, GetInterfacesReturnFailure) {
  MockIw(IwCommand::kDev, "", EXIT_FAILURE, "Something wrong!!!");

  auto result = FetchNetworkInterfaceInfoSync();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kSystemUtilityError);
}

// Test case: GetLink return failure.
TEST_F(NetworkInterfaceFetcherTest, GetLinkReturnFailure) {
  MockIw(IwCommand::kLink, kExpectedInterfaceName, EXIT_FAILURE,
         "Something wrong!!!");

  auto result = FetchNetworkInterfaceInfoSync();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kSystemUtilityError);
}

// Test case: GetInfo return failure.
TEST_F(NetworkInterfaceFetcherTest, GetInfoReturnFailure) {
  MockIw(IwCommand::kInfo, kExpectedInterfaceName, EXIT_FAILURE,
         "Something wrong!!!");

  auto result = FetchNetworkInterfaceInfoSync();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kSystemUtilityError);
}

// Test case: GetScanDump return failure.
TEST_F(NetworkInterfaceFetcherTest, GetScanDumpReturnFailure) {
  MockIw(IwCommand::kScanDump, kExpectedInterfaceName, EXIT_FAILURE,
         "Something wrong!!!");

  auto result = FetchNetworkInterfaceInfoSync();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kSystemUtilityError);
}

// Test case: wireless device not connected to an access point. Expecting only
// non-link data is available.
TEST_F(NetworkInterfaceFetcherTest, WirelessNotConnected) {
  MockIw(IwCommand::kLink, kExpectedInterfaceName, EXIT_SUCCESS,
         kFakeGetLinkDeviceNotConnectedOutput);

  auto result = FetchNetworkInterfaceInfoSync();
  ASSERT_TRUE(result->is_network_interface_info());
  const auto& network_infos = result->get_network_interface_info();
  ASSERT_THAT(network_infos, Not(IsEmpty()));
  const auto& network_info = network_infos.at(0);
  ASSERT_FALSE(network_info.is_null());
  switch (network_info->which()) {
    case mojom::NetworkInterfaceInfo::Tag::kWirelessInterfaceInfo: {
      const auto& wireless_info = network_info->get_wireless_interface_info();
      ASSERT_FALSE(wireless_info.is_null());
      EXPECT_EQ(wireless_info->interface_name, kExpectedInterfaceName);
      EXPECT_EQ(wireless_info->power_management_on, kExpectedPowerManagementOn);
      const auto& link_info = wireless_info->wireless_link_info;
      ASSERT_TRUE(link_info.is_null());
      break;
    }
  }
}

// Test case: wireless adapter not found.
TEST_F(NetworkInterfaceFetcherTest, NoWirelessAdapterFound) {
  MockIw(IwCommand::kDev, "", EXIT_SUCCESS,
         kFakeGetInterfacesNoWirelessAdapterOutput);

  auto result = FetchNetworkInterfaceInfoSync();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kServiceUnavailable);
}

// Test case: missing /sys/module/iwlmvm/parameters/power_scheme file.
TEST_F(NetworkInterfaceFetcherTest, MissingPowerSchemeFile) {
  MockReadPowerSchema(std::nullopt);

  auto result = FetchNetworkInterfaceInfoSync();
  ASSERT_TRUE(result->is_network_interface_info());
  const auto& network_infos = result->get_network_interface_info();
  ASSERT_THAT(network_infos, Not(IsEmpty()));
  const auto& network_info = network_infos.at(0);

  ASSERT_FALSE(network_info.is_null());
  switch (network_info->which()) {
    case mojom::NetworkInterfaceInfo::Tag::kWirelessInterfaceInfo: {
      const auto& wireless_info = network_info->get_wireless_interface_info();
      EXPECT_FALSE(wireless_info->power_management_on);
      break;
    }
  }
}

// Test case: missing rx bitrate is set to 0.
TEST_F(NetworkInterfaceFetcherTest, MissingRxBitrateSetToZero) {
  MockIw(IwCommand::kLink, kExpectedInterfaceName, EXIT_SUCCESS,
         kFakeGetLinkOutputWithoutRxBitrate);

  auto result = FetchNetworkInterfaceInfoSync();
  ASSERT_TRUE(result->is_network_interface_info());
  const auto& network_infos = result->get_network_interface_info();
  ASSERT_THAT(network_infos, Not(IsEmpty()));
  const auto& network_info = network_infos.at(0);
  ASSERT_FALSE(network_info.is_null());
  EXPECT_TRUE(network_info->is_wireless_interface_info());
  const auto& wireless_info = network_info->get_wireless_interface_info();
  ASSERT_FALSE(wireless_info.is_null());
  const auto& link_info = wireless_info->wireless_link_info;
  ASSERT_FALSE(link_info.is_null());
  EXPECT_EQ(link_info->rx_bit_rate_mbps, 0);
}

}  // namespace diagnostics
