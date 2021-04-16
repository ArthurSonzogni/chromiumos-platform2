// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/wireguard_driver.h"

#include <memory>
#include <utility>

#include <base/files/scoped_temp_dir.h>
#include <base/files/file_util.h>
#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest.h>

#include "shill/fake_store.h"
#include "shill/mock_control.h"
#include "shill/mock_device_info.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_process_manager.h"
#include "shill/property_store.h"
#include "shill/test_event_dispatcher.h"
#include "shill/vpn/mock_vpn_driver.h"

namespace shill {

using testing::_;
using testing::DoAll;
using testing::HasSubstr;
using testing::NiceMock;
using testing::Not;
using testing::Return;
using testing::SaveArg;

// Expose necessary private members of WireguardDriver for testing.
class WireguardDriverTestPeer {
 public:
  explicit WireguardDriverTestPeer(WireguardDriver* driver) : driver_(driver) {}

  void PrepareForConfigFile() {
    // Creates a temp directory for storing the config file.
    CHECK(scoped_temp_dir_.CreateUniqueTempDir());
    driver_->config_directory_ = scoped_temp_dir_.GetPath();

    // Deactivates chown() calls.
    driver_->vpn_gid_ = -1;
  }

 private:
  WireguardDriver* driver_;
  base::ScopedTempDir scoped_temp_dir_;
};

// This function should exist outside the anonymous namespace, otherwise it
// cannot be found by the gmock framework.
bool operator==(const IPConfig::Route& lhs, const IPConfig::Route& rhs) {
  return lhs.host == rhs.host && lhs.prefix == rhs.prefix &&
         lhs.gateway == rhs.gateway;
}

namespace {

constexpr pid_t kWireguardPid = 12345;
constexpr pid_t kWireguardToolsPid = 12346;
const char kIfName[] = "wg0";
constexpr int kIfIndex = 123;

// Consistent with the properties set in
// WireguardDriverTest::InitializePropertyStore().
const char kExpectedConfigFileContents[] = R"([Interface]
PrivateKey=private-key

[Peer]
PublicKey=public-key-1
PresharedKey=preshared-key-1
EndPoint=10.0.1.1:12345
AllowedIPs=192.168.1.2/32,192.168.2.0/24
PersistentKeepalive=10

[Peer]
PublicKey=public-key-2
EndPoint=10.0.1.2:12345
AllowedIPs=192.168.1.2/32,192.168.3.0/24
)";

class WireguardDriverTest : public testing::Test {
 public:
  WireguardDriverTest()
      : manager_(&control_, &dispatcher_, &metrics_),
        device_info_(&manager_),
        driver_(new WireguardDriver(&manager_, &process_manager_)),
        driver_test_peer_(driver_.get()) {
    manager_.set_mock_device_info(&device_info_);
    driver_test_peer_.PrepareForConfigFile();
  }

 protected:
  void InitializePropertyStore() {
    driver_->InitPropertyStore(&property_store_);
    Error err;
    property_store_.SetStringProperty(kWireguardPrivateKey, "private-key",
                                      &err);
    property_store_.SetStringProperty(kWireguardAddress, "192.168.1.2", &err);
    property_store_.SetStringmapsProperty(
        kWireguardPeers,
        {
            {{kWireguardPeerPublicKey, "public-key-1"},
             {kWireguardPeerPresharedKey, "preshared-key-1"},
             {kWireguardPeerPersistentKeepalive, "10"},
             {kWireguardPeerEndPoint, "10.0.1.1:12345"},
             {kWireguardPeerAllowedIPs, "192.168.1.2/32,192.168.2.0/24"}},
            {{kWireguardPeerPublicKey, "public-key-2"},
             {kWireguardPeerEndPoint, "10.0.1.2:12345"},
             {kWireguardPeerAllowedIPs, "192.168.1.2/32,192.168.3.0/24"}},
        },
        &err);
  }

  void InvokeConnectAsync() {
    driver_->ConnectAsync(&driver_event_handler_);
    EXPECT_CALL(device_info_, AddVirtualInterfaceReadyCallback(kIfName, _))
        .WillOnce([this](const std::string&, DeviceInfo::LinkReadyCallback cb) {
          this->link_ready_callback_ = std::move(cb);
        });
    EXPECT_CALL(
        process_manager_,
        StartProcessInMinijail(_, _, _, _, "vpn", "vpn",
                               CAP_TO_MASK(CAP_NET_ADMIN), true, true, _))
        .WillOnce(DoAll(SaveArg<9>(&wireguard_exit_callback_),
                        Return(kWireguardPid)));
    dispatcher_.DispatchPendingEvents();
  }

  void InvokeLinkReady() {
    // wireguard-tools should be invoked on interface ready.
    std::vector<std::string> args;
    EXPECT_CALL(process_manager_,
                StartProcessInMinijail(_, base::FilePath("/usr/sbin/wg"), _, _,
                                       "vpn", "vpn", 0, true, true, _))
        .WillOnce(DoAll(SaveArg<2>(&args),
                        SaveArg<9>(&wireguard_tools_exit_callback_),
                        Return(kWireguardToolsPid)));
    std::move(link_ready_callback_).Run(kIfName, kIfIndex);

    EXPECT_EQ(args[0], "setconf");
    EXPECT_EQ(args[1], kIfName);
    config_file_path_ = base::FilePath(args[2]);
    EXPECT_TRUE(base::PathExists(config_file_path_));
  }

  MockControl control_;
  EventDispatcherForTest dispatcher_;
  MockMetrics metrics_;
  MockProcessManager process_manager_;
  MockManager manager_;
  NiceMock<MockDeviceInfo> device_info_;
  FakeStore fake_store_;
  PropertyStore property_store_;
  MockVPNDriverEventHandler driver_event_handler_;
  std::unique_ptr<WireguardDriver> driver_;
  WireguardDriverTestPeer driver_test_peer_;

  base::RepeatingCallback<void(int)> wireguard_exit_callback_;
  base::RepeatingCallback<void(int)> wireguard_tools_exit_callback_;
  DeviceInfo::LinkReadyCallback link_ready_callback_;
  base::FilePath config_file_path_;
};

TEST_F(WireguardDriverTest, ConnectFlow) {
  InitializePropertyStore();
  InvokeConnectAsync();
  InvokeLinkReady();

  // Configuration done.
  EXPECT_CALL(driver_event_handler_, OnDriverConnected(kIfName, kIfIndex));
  wireguard_tools_exit_callback_.Run(0);

  // Checks config file content.
  std::string contents;
  CHECK(base::ReadFileToString(config_file_path_, &contents));
  EXPECT_EQ(contents, kExpectedConfigFileContents);

  // Checks IPProperties.
  const auto& ip_properties = driver_->GetIPProperties();
  EXPECT_EQ(ip_properties.address_family, shill::IPAddress::kFamilyIPv4);
  EXPECT_EQ(ip_properties.address, "192.168.1.2");
  EXPECT_THAT(ip_properties.routes,
              testing::UnorderedElementsAre(
                  // We do not dedup so this entry appears twice.
                  IPConfig::Route("192.168.1.2", 32, "0.0.0.0"),
                  IPConfig::Route("192.168.1.2", 32, "0.0.0.0"),
                  IPConfig::Route("192.168.2.0", 24, "0.0.0.0"),
                  IPConfig::Route("192.168.3.0", 24, "0.0.0.0")));

  // Disconnect.
  EXPECT_CALL(process_manager_, StopProcess(kWireguardPid));
  driver_->Disconnect();

  // Checks that the config file has been deleted.
  EXPECT_FALSE(base::PathExists(config_file_path_));
}

TEST_F(WireguardDriverTest, WireguardToolsFailed) {
  InitializePropertyStore();
  InvokeConnectAsync();
  InvokeLinkReady();

  // Configuration failed.
  EXPECT_CALL(driver_event_handler_, OnDriverFailure(_, _));
  wireguard_tools_exit_callback_.Run(1);

  // Checks that the config file has been deleted.
  EXPECT_FALSE(base::PathExists(config_file_path_));
}

TEST_F(WireguardDriverTest, PropertyStoreAndConfigFile) {
  std::string contents;
  Error err;
  InitializePropertyStore();

  // Save & load should not lose any information.
  const std::string kStorageId = "wireguard-test";
  driver_->Save(&fake_store_, kStorageId, /*save_credentials=*/true);
  driver_->Load(&fake_store_, kStorageId);
  InvokeConnectAsync();
  InvokeLinkReady();
  CHECK(base::ReadFileToString(config_file_path_, &contents));
  EXPECT_EQ(contents, kExpectedConfigFileContents);
  driver_->Disconnect();

  // Checks reading properties. Private keys and preshared keys should not be
  // readable.
  KeyValueStore provider;
  EXPECT_TRUE(property_store_.GetKeyValueStoreProperty(kProviderProperty,
                                                       &provider, &err));
  EXPECT_FALSE(provider.Contains<std::string>(kWireguardPrivateKey));
  EXPECT_EQ(provider.Get<std::string>(kWireguardAddress), "192.168.1.2");
  EXPECT_EQ(provider.Get<Stringmaps>(kWireguardPeers),
            (Stringmaps{
                {{kWireguardPeerPublicKey, "public-key-1"},
                 {kWireguardPeerPersistentKeepalive, "10"},
                 {kWireguardPeerEndPoint, "10.0.1.1:12345"},
                 {kWireguardPeerAllowedIPs, "192.168.1.2/32,192.168.2.0/24"}},
                {{kWireguardPeerPublicKey, "public-key-2"},
                 {kWireguardPeerPersistentKeepalive, ""},
                 {kWireguardPeerEndPoint, "10.0.1.2:12345"},
                 {kWireguardPeerAllowedIPs, "192.168.1.2/32,192.168.3.0/24"}},
            }));

  // Setting peers without touching PresharedKey property should leave it
  // unchanged.
  property_store_.SetStringmapsProperty(
      kWireguardPeers,
      {
          {{kWireguardPeerPublicKey, "public-key-1"},
           {kWireguardPeerPersistentKeepalive, "10"},
           {kWireguardPeerEndPoint, "10.0.1.1:12345"},
           {kWireguardPeerAllowedIPs, "192.168.1.2/32,192.168.2.0/24"}},
          {{kWireguardPeerPublicKey, "public-key-2"},
           {kWireguardPeerEndPoint, "10.0.1.2:12345"},
           {kWireguardPeerAllowedIPs, "192.168.1.2/32,192.168.3.0/24"}},
      },
      &err);
  InvokeConnectAsync();
  InvokeLinkReady();
  CHECK(base::ReadFileToString(config_file_path_, &contents));
  EXPECT_EQ(contents, kExpectedConfigFileContents);
  driver_->Disconnect();

  // Setting peers with an empty PresharedKey property should clear the
  // PresharedKey of that peer.
  property_store_.SetStringmapsProperty(
      kWireguardPeers,
      {
          {{kWireguardPeerPublicKey, "public-key-1"},
           {kWireguardPeerPresharedKey, ""},
           {kWireguardPeerPersistentKeepalive, "10"},
           {kWireguardPeerEndPoint, "10.0.1.1:12345"},
           {kWireguardPeerAllowedIPs, "192.168.1.2/32,192.168.2.0/24"}},
          {{kWireguardPeerPublicKey, "public-key-2"},
           {kWireguardPeerEndPoint, "10.0.1.2:12345"},
           {kWireguardPeerAllowedIPs, "192.168.1.2/32,192.168.3.0/24"}},
      },
      &err);
  InvokeConnectAsync();
  InvokeLinkReady();
  CHECK(base::ReadFileToString(config_file_path_, &contents));
  EXPECT_THAT(contents, Not(HasSubstr("PresharedKey=")));
}

TEST_F(WireguardDriverTest, DisconnectBeforeConnected) {
  InvokeConnectAsync();
  EXPECT_CALL(process_manager_, StopProcess(kWireguardPid));
  driver_->Disconnect();
}

TEST_F(WireguardDriverTest, SpawnWireguardProcessFailed) {
  driver_->ConnectAsync(&driver_event_handler_);
  EXPECT_CALL(process_manager_,
              StartProcessInMinijail(_, _, _, _, _, _, _, _, _, _))
      .WillOnce(Return(-1));
  EXPECT_CALL(driver_event_handler_, OnDriverFailure(_, _));
  dispatcher_.DispatchPendingEvents();
}

TEST_F(WireguardDriverTest, WireguardProcessExitedUnexpectedly) {
  InvokeConnectAsync();
  EXPECT_CALL(driver_event_handler_, OnDriverFailure(_, _));
  wireguard_exit_callback_.Run(1);
}

TEST_F(WireguardDriverTest, OnConnectTimeout) {
  InvokeConnectAsync();
  EXPECT_CALL(driver_event_handler_, OnDriverFailure(_, _));
  driver_->OnConnectTimeout();
}

}  // namespace
}  // namespace shill
