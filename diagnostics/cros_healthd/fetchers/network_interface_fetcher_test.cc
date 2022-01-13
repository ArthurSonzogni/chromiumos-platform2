// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/run_loop.h>
#include <base/strings/stringprintf.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/cros_healthd/executor/mock_executor_adapter.h"
#include "diagnostics/cros_healthd/fetchers/network_interface_fetcher.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/private/cros_healthd_executor.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

namespace executor_ipc = chromeos::cros_healthd_executor::mojom;
namespace mojo_ipc = chromeos::cros_healthd::mojom;

using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::StrictMock;
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

// Saves |response| to |response_destination|.
void OnGetNetworkInterfaceResponse(
    mojo_ipc::NetworkInterfaceResultPtr* response_update,
    mojo_ipc::NetworkInterfaceResultPtr response) {
  *response_update = std::move(response);
}

}  // namespace

class NetworkInterfaceFetcherTest : public ::testing::Test {
 protected:
  NetworkInterfaceFetcherTest() = default;

  void SetUp() override {
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        root_dir().Append(kRelativeWirelessPowerSchemePath),
        kFakePowerSchemeContent));
  }

  const base::FilePath& root_dir() { return mock_context_.root_dir(); }

  MockExecutorAdapter* mock_executor() { return mock_context_.mock_executor(); }

  mojo_ipc::NetworkInterfaceResultPtr FetchNetworkInterfaceInfo() {
    base::RunLoop run_loop;
    mojo_ipc::NetworkInterfaceResultPtr result;
    network_interface_fetcher_.FetchNetworkInterfaceInfo(
        base::BindOnce(&OnGetNetworkInterfaceResponse, &result));
    run_loop.RunUntilIdle();
    return result;
  }

 private:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY};
  MockContext mock_context_;
  NetworkInterfaceFetcher network_interface_fetcher_{&mock_context_};
};

// Test TestFetchNetworkInterfaceInfo matching with expected result.
TEST_F(NetworkInterfaceFetcherTest, TestFetchNetworkInterfaceInfo) {
  // Set the mock executor response for GetInterfaces.
  EXPECT_CALL(*mock_executor(), GetInterfaces(_))
      .WillOnce(WithArg<0>(
          Invoke([](executor_ipc::Executor::GetInterfacesCallback callback) {
            executor_ipc::ProcessResult result;
            result.return_code = EXIT_SUCCESS;
            result.out = base::StringPrintf("%s", kFakeGetInterfacesOutput);
            std::move(callback).Run(result.Clone());
          })));

  // Set the mock executor response for GetLink.
  EXPECT_CALL(*mock_executor(), GetLink(_, _))
      .WillOnce(Invoke([](const std::string& interface_name,
                          executor_ipc::Executor::GetLinkCallback callback) {
        executor_ipc::ProcessResult result;
        result.return_code = EXIT_SUCCESS;
        result.out = base::StringPrintf("%s", kFakeGetLinkOutput);
        std::move(callback).Run(result.Clone());
      }));

  // Set the mock executor response for GetInfo.
  EXPECT_CALL(*mock_executor(), GetInfo(_, _))
      .WillOnce(Invoke([](const std::string& interface_name,
                          executor_ipc::Executor::GetInfoCallback callback) {
        executor_ipc::ProcessResult result;
        result.return_code = EXIT_SUCCESS;
        result.out = base::StringPrintf("%s", kFakeGetInfoOutput);
        std::move(callback).Run(result.Clone());
      }));

  // Set the mock executor response for GetScanDump.
  EXPECT_CALL(*mock_executor(), GetScanDump(_, _))
      .WillOnce(
          Invoke([](const std::string& interface_name,
                    executor_ipc::Executor::GetScanDumpCallback callback) {
            executor_ipc::ProcessResult result;
            result.return_code = EXIT_SUCCESS;
            result.out = base::StringPrintf("%s", kFakeGetScanDumpOutput);
            std::move(callback).Run(result.Clone());
          }));

  auto result = FetchNetworkInterfaceInfo();

  ASSERT_TRUE(result->is_network_interface_info());
  const auto& network_infos = result->get_network_interface_info();
  const auto& network_info = network_infos.at(0);

  ASSERT_FALSE(network_info.is_null());
  switch (network_info->which()) {
    case mojo_ipc::NetworkInterfaceInfo::Tag::WIRELESS_INTERFACE_INFO: {
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
TEST_F(NetworkInterfaceFetcherTest, TestGetInterfacesReturnFailure) {
  // Set the mock executor response for GetInterfaces.
  EXPECT_CALL(*mock_executor(), GetInterfaces(_))
      .WillOnce(WithArg<0>(
          Invoke([](executor_ipc::Executor::GetInterfacesCallback callback) {
            executor_ipc::ProcessResult result;
            result.return_code = EXIT_FAILURE;
            result.out = "Something wrong!!!";
            std::move(callback).Run(result.Clone());
          })));

  auto result = FetchNetworkInterfaceInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kSystemUtilityError);
}

// Test case: GetLink return failure.
TEST_F(NetworkInterfaceFetcherTest, TestGetLinkReturnFailure) {
  // Set the mock executor response for GetInterfaces.
  EXPECT_CALL(*mock_executor(), GetInterfaces(_))
      .WillOnce(WithArg<0>(
          Invoke([](executor_ipc::Executor::GetInterfacesCallback callback) {
            executor_ipc::ProcessResult result;
            result.return_code = EXIT_SUCCESS;
            result.out = base::StringPrintf("%s", kFakeGetInterfacesOutput);
            std::move(callback).Run(result.Clone());
          })));

  // Set the mock executor response for GetLink.
  EXPECT_CALL(*mock_executor(), GetLink(_, _))
      .WillOnce(Invoke([](const std::string& interface_name,
                          executor_ipc::Executor::GetLinkCallback callback) {
        executor_ipc::ProcessResult result;
        result.return_code = EXIT_FAILURE;
        result.out = "Something wrong!!!";
        std::move(callback).Run(result.Clone());
      }));

  auto result = FetchNetworkInterfaceInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kSystemUtilityError);
}

// Test case: GetInfo return failure.
TEST_F(NetworkInterfaceFetcherTest, TestGetInfoReturnFailure) {
  // Set the mock executor response for GetInterfaces.
  EXPECT_CALL(*mock_executor(), GetInterfaces(_))
      .WillOnce(WithArg<0>(
          Invoke([](executor_ipc::Executor::GetInterfacesCallback callback) {
            executor_ipc::ProcessResult result;
            result.return_code = EXIT_SUCCESS;
            result.out = base::StringPrintf("%s", kFakeGetInterfacesOutput);
            std::move(callback).Run(result.Clone());
          })));

  // Set the mock executor response for GetLink.
  EXPECT_CALL(*mock_executor(), GetLink(_, _))
      .WillOnce(Invoke([](const std::string& interface_name,
                          executor_ipc::Executor::GetLinkCallback callback) {
        executor_ipc::ProcessResult result;
        result.return_code = EXIT_SUCCESS;
        result.out = base::StringPrintf("%s", kFakeGetLinkOutput);
        std::move(callback).Run(result.Clone());
      }));

  // Set the mock executor response for GetInfo.
  EXPECT_CALL(*mock_executor(), GetInfo(_, _))
      .WillOnce(Invoke([](const std::string& interface_name,
                          executor_ipc::Executor::GetInfoCallback callback) {
        executor_ipc::ProcessResult result;
        result.return_code = EXIT_FAILURE;
        result.out = "Something wrong!!!";
        std::move(callback).Run(result.Clone());
      }));

  auto result = FetchNetworkInterfaceInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kSystemUtilityError);
}

// Test case: GetScanDump return failure.
TEST_F(NetworkInterfaceFetcherTest, TestGetScanDumpReturnFailure) {
  // Set the mock executor response for GetInterfaces.
  EXPECT_CALL(*mock_executor(), GetInterfaces(_))
      .WillOnce(WithArg<0>(
          Invoke([](executor_ipc::Executor::GetInterfacesCallback callback) {
            executor_ipc::ProcessResult result;
            result.return_code = EXIT_SUCCESS;
            result.out = base::StringPrintf("%s", kFakeGetInterfacesOutput);
            std::move(callback).Run(result.Clone());
          })));

  // Set the mock executor response for GetLink.
  EXPECT_CALL(*mock_executor(), GetLink(_, _))
      .WillOnce(Invoke([](const std::string& interface_name,
                          executor_ipc::Executor::GetLinkCallback callback) {
        executor_ipc::ProcessResult result;
        result.return_code = EXIT_SUCCESS;
        result.out = base::StringPrintf("%s", kFakeGetLinkOutput);
        std::move(callback).Run(result.Clone());
      }));

  // Set the mock executor response for GetInfo.
  EXPECT_CALL(*mock_executor(), GetInfo(_, _))
      .WillOnce(Invoke([](const std::string& interface_name,
                          executor_ipc::Executor::GetInfoCallback callback) {
        executor_ipc::ProcessResult result;
        result.return_code = EXIT_SUCCESS;
        result.out = base::StringPrintf("%s", kFakeGetInfoOutput);
        std::move(callback).Run(result.Clone());
      }));

  // Set the mock executor response for GetScanDump.
  EXPECT_CALL(*mock_executor(), GetScanDump(_, _))
      .WillOnce(
          Invoke([](const std::string& interface_name,
                    executor_ipc::Executor::GetScanDumpCallback callback) {
            executor_ipc::ProcessResult result;
            result.return_code = EXIT_FAILURE;
            result.out = "Something wrong!!!";
            std::move(callback).Run(result.Clone());
          }));

  auto result = FetchNetworkInterfaceInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kSystemUtilityError);
}

// Test case: wireless device not connected to an access point. Expecting only
// non-link data is available.
TEST_F(NetworkInterfaceFetcherTest, TestWirelessNotConnected) {
  // Set the mock executor response for GetInterfaces.
  EXPECT_CALL(*mock_executor(), GetInterfaces(_))
      .WillOnce(WithArg<0>(
          Invoke([](executor_ipc::Executor::GetInterfacesCallback callback) {
            executor_ipc::ProcessResult result;
            result.return_code = EXIT_SUCCESS;
            result.out = base::StringPrintf("%s", kFakeGetInterfacesOutput);
            std::move(callback).Run(result.Clone());
          })));

  // Set the mock executor response for GetLink.
  EXPECT_CALL(*mock_executor(), GetLink(_, _))
      .WillOnce(Invoke([](const std::string& interface_name,
                          executor_ipc::Executor::GetLinkCallback callback) {
        executor_ipc::ProcessResult result;
        result.return_code = EXIT_SUCCESS;
        result.out =
            base::StringPrintf("%s", kFakeGetLinkDeviceNotConnectedOutput);
        std::move(callback).Run(result.Clone());
      }));

  auto result = FetchNetworkInterfaceInfo();
  ASSERT_TRUE(result->is_network_interface_info());
  const auto& network_infos = result->get_network_interface_info();
  const auto& network_info = network_infos.at(0);
  ASSERT_FALSE(network_info.is_null());
  switch (network_info->which()) {
    case mojo_ipc::NetworkInterfaceInfo::Tag::WIRELESS_INTERFACE_INFO: {
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
TEST_F(NetworkInterfaceFetcherTest, TestNoWirelessAdapterFound) {
  // Set the mock executor response for GetInterfaces.
  EXPECT_CALL(*mock_executor(), GetInterfaces(_))
      .WillOnce(WithArg<0>(
          Invoke([](executor_ipc::Executor::GetInterfacesCallback callback) {
            executor_ipc::ProcessResult result;
            result.return_code = EXIT_SUCCESS;
            result.out = base::StringPrintf(
                "%s", kFakeGetInterfacesNoWirelessAdapterOutput);
            std::move(callback).Run(result.Clone());
          })));

  auto result = FetchNetworkInterfaceInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kServiceUnavailable);
}

// Test case: missing /sys/module/iwlmvm/parameters/power_scheme file.
TEST_F(NetworkInterfaceFetcherTest, TestMissingPowerSchemeFile) {
  ASSERT_TRUE(
      base::DeleteFile(root_dir().Append(kRelativeWirelessPowerSchemePath)));
  // Set the mock executor response for GetInterfaces.
  EXPECT_CALL(*mock_executor(), GetInterfaces(_))
      .WillOnce(WithArg<0>(
          Invoke([](executor_ipc::Executor::GetInterfacesCallback callback) {
            executor_ipc::ProcessResult result;
            result.return_code = EXIT_SUCCESS;
            result.out = base::StringPrintf("%s", kFakeGetInterfacesOutput);
            std::move(callback).Run(result.Clone());
          })));

  auto result = FetchNetworkInterfaceInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kFileReadError);
}

}  // namespace diagnostics
