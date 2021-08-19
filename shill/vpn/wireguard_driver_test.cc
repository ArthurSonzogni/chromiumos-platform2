// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/wireguard_driver.h"

#include <map>
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
#include "shill/vpn/fake_vpn_util.h"
#include "shill/vpn/mock_vpn_driver.h"

namespace shill {

using testing::_;
using testing::DoAll;
using testing::HasSubstr;
using testing::NiceMock;
using testing::Not;
using testing::Return;
using testing::SaveArg;

// Expose necessary private members of WireGuardDriver for testing.
class WireGuardDriverTestPeer {
 public:
  explicit WireGuardDriverTestPeer(WireGuardDriver* driver) : driver_(driver) {
    // Creates a temp directory for storing the config file.
    CHECK(scoped_temp_dir_.CreateUniqueTempDir());
    driver_->config_directory_ = scoped_temp_dir_.GetPath();

    driver->vpn_util_ = std::make_unique<FakeVPNUtil>();
  }

  const Stringmaps& peers() { return driver_->peers_; }

 private:
  std::unique_ptr<WireGuardDriver> driver_;
  base::ScopedTempDir scoped_temp_dir_;
};

// This function should exist outside the anonymous namespace, otherwise it
// cannot be found by the gmock framework.
bool operator==(const IPConfig::Route& lhs, const IPConfig::Route& rhs) {
  return lhs.host == rhs.host && lhs.prefix == rhs.prefix &&
         lhs.gateway == rhs.gateway;
}

namespace {

constexpr pid_t kWireGuardPid = 12345;
constexpr pid_t kWireGuardToolsPid = 12346;
const char kIfName[] = "wg0";
constexpr int kIfIndex = 123;

// Randomly generated key for testing.
constexpr char kPrivateKey1[] = "gOL/kVF88Mdr7rVM2Fz91UgyAW4L8iYogU/M+9hlKmM=";
constexpr char kPrivateKey2[] = "wARBVZOPBWo7OoyHLfv2mDgFxYJ3S6uc9lIOpRiGqVI=";

// Consistent with the properties set in
// WireGuardDriverTest::InitializePropertyStore().
const char kExpectedConfigFileContents[] = R"([Interface]
PrivateKey=gOL/kVF88Mdr7rVM2Fz91UgyAW4L8iYogU/M+9hlKmM=
FwMark=0x4500

[Peer]
PublicKey=public-key-1
PresharedKey=preshared-key-1
Endpoint=10.0.1.1:12345
AllowedIPs=192.168.1.2/32,192.168.2.0/24
PersistentKeepalive=10

[Peer]
PublicKey=public-key-2
Endpoint=10.0.1.2:12345
AllowedIPs=192.168.1.2/32,192.168.3.0/24
)";

class WireGuardDriverTest : public testing::Test {
 public:
  WireGuardDriverTest()
      : manager_(&control_, &dispatcher_, &metrics_), device_info_(&manager_) {
    ResetDriver();
    manager_.set_mock_device_info(&device_info_);
    SetFakeKeyGenerator();
  }

 protected:
  // Useful in storage-related tests.
  void ResetDriver() {
    driver_ = new WireGuardDriver(&manager_, &process_manager_);
    driver_test_peer_.reset(new WireGuardDriverTestPeer(driver_));
    property_store_.reset(new PropertyStore());
    driver_->InitPropertyStore(property_store_.get());
  }

  void InitializePropertyStore() {
    Error err;
    property_store_->SetStringProperty(kWireGuardPrivateKey, kPrivateKey1,
                                       &err);
    property_store_->SetStringmapsProperty(
        kWireGuardPeers,
        {
            {{kWireGuardPeerPublicKey, "public-key-1"},
             {kWireGuardPeerPresharedKey, "preshared-key-1"},
             {kWireGuardPeerPersistentKeepalive, "10"},
             {kWireGuardPeerEndpoint, "10.0.1.1:12345"},
             {kWireGuardPeerAllowedIPs, "192.168.1.2/32,192.168.2.0/24"}},
            {{kWireGuardPeerPublicKey, "public-key-2"},
             {kWireGuardPeerEndpoint, "10.0.1.2:12345"},
             {kWireGuardPeerAllowedIPs, "192.168.1.2/32,192.168.3.0/24"}},
        },
        &err);
  }

  // Whenever the driver asks for calculating a public key, just echoes the
  // input back.
  // TODO(jiejiang): Consider returning a different key to avoid missing the
  // failure that keys are misused.
  void SetFakeKeyGenerator() {
    EXPECT_CALL(process_manager_, StartProcessInMinijailWithPipes(
                                      _, base::FilePath("/usr/bin/wg"),
                                      std::vector<std::string>{"pubkey"}, _,
                                      "vpn", "vpn", 0, true, true, _, _))
        .WillRepeatedly([](const base::Location&, const base::FilePath&,
                           const std::vector<std::string>&,
                           const std::map<std::string, std::string>&,
                           const std::string&, const std::string&, uint64_t,
                           bool, bool, const base::Callback<void(int)>&,
                           struct std_file_descriptors std_fds) {
          CHECK(std_fds.stdin_fd);
          CHECK(std_fds.stdout_fd);
          int echo_pipe[2];
          CHECK_EQ(pipe(echo_pipe), 0);
          *std_fds.stdin_fd = echo_pipe[1];
          *std_fds.stdout_fd = echo_pipe[0];
          return 0;
        });
  }

  void InvokeConnectAsyncKernel() {
    driver_->ConnectAsync(&driver_event_handler_);
    EXPECT_CALL(device_info_, CreateWireGuardInterface(kIfName, _, _))
        .WillOnce([this](const std::string&,
                         DeviceInfo::LinkReadyCallback link_ready_cb,
                         base::OnceClosure failure_cb) {
          this->link_ready_callback_ = std::move(link_ready_cb);
          this->create_kernel_link_failed_callback_ = std::move(failure_cb);
          return true;
        });
    dispatcher_.DispatchPendingEvents();
  }

  void InvokeConnectAsyncUserspace() {
    driver_->ConnectAsync(&driver_event_handler_);
    EXPECT_CALL(device_info_, CreateWireGuardInterface(kIfName, _, _))
        .WillOnce([this](const std::string&,
                         DeviceInfo::LinkReadyCallback link_ready_cb,
                         base::OnceClosure failure_cb) {
          this->link_ready_callback_ = std::move(link_ready_cb);
          this->create_kernel_link_failed_callback_ = std::move(failure_cb);
          return true;
        });
    EXPECT_CALL(device_info_, AddVirtualInterfaceReadyCallback(kIfName, _))
        .WillOnce([this](const std::string&, DeviceInfo::LinkReadyCallback cb) {
          this->link_ready_callback_ = std::move(cb);
        });
    EXPECT_CALL(
        process_manager_,
        StartProcessInMinijail(_, _, _, _, "vpn", "vpn",
                               CAP_TO_MASK(CAP_NET_ADMIN), true, true, _))
        .WillOnce(DoAll(SaveArg<9>(&wireguard_exit_callback_),
                        Return(kWireGuardPid)));
    dispatcher_.DispatchPendingEvents();
    std::move(create_kernel_link_failed_callback_).Run();
  }

  void InvokeLinkReady() {
    // wireguard-tools should be invoked on interface ready.
    std::vector<std::string> args;
    EXPECT_CALL(process_manager_,
                StartProcessInMinijail(_, base::FilePath("/usr/bin/wg"), _, _,
                                       "vpn", "vpn", CAP_TO_MASK(CAP_NET_ADMIN),
                                       true, true, _))
        .WillOnce(DoAll(SaveArg<2>(&args),
                        SaveArg<9>(&wireguard_tools_exit_callback_),
                        Return(kWireGuardToolsPid)));
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
  std::unique_ptr<PropertyStore> property_store_;
  MockVPNDriverEventHandler driver_event_handler_;
  WireGuardDriver* driver_;  // owned by driver_test_peer_
  std::unique_ptr<WireGuardDriverTestPeer> driver_test_peer_;

  base::RepeatingCallback<void(int)> wireguard_exit_callback_;
  base::RepeatingCallback<void(int)> wireguard_tools_exit_callback_;
  DeviceInfo::LinkReadyCallback link_ready_callback_;
  base::OnceClosure create_kernel_link_failed_callback_;
  base::FilePath config_file_path_;
};

TEST_F(WireGuardDriverTest, ConnectFlowKernel) {
  InitializePropertyStore();
  InvokeConnectAsyncKernel();
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
  EXPECT_THAT(ip_properties.routes,
              testing::UnorderedElementsAre(
                  // We do not dedup so this entry appears twice.
                  IPConfig::Route("192.168.1.2", 32, "0.0.0.0"),
                  IPConfig::Route("192.168.1.2", 32, "0.0.0.0"),
                  IPConfig::Route("192.168.2.0", 24, "0.0.0.0"),
                  IPConfig::Route("192.168.3.0", 24, "0.0.0.0")));

  // Disconnect.
  EXPECT_CALL(device_info_, DeleteInterface(kIfIndex));
  driver_->Disconnect();

  // Checks that the config file has been deleted.
  EXPECT_FALSE(base::PathExists(config_file_path_));
}

TEST_F(WireGuardDriverTest, ConnectFlowUserspace) {
  InitializePropertyStore();
  InvokeConnectAsyncUserspace();
  InvokeLinkReady();

  // Configuration done.
  EXPECT_CALL(driver_event_handler_, OnDriverConnected(kIfName, kIfIndex));
  wireguard_tools_exit_callback_.Run(0);

  // Checks config file content.
  std::string contents;
  CHECK(base::ReadFileToString(config_file_path_, &contents));
  EXPECT_EQ(contents, kExpectedConfigFileContents);

  // Skips checks for IPProperties. See ConnectFlowKernel.

  // Disconnect.
  EXPECT_CALL(process_manager_, StopProcess(kWireGuardPid));
  driver_->Disconnect();

  // Checks that the config file has been deleted.
  EXPECT_FALSE(base::PathExists(config_file_path_));
}

TEST_F(WireGuardDriverTest, WireGuardToolsFailed) {
  InitializePropertyStore();
  InvokeConnectAsyncKernel();
  InvokeLinkReady();

  // Configuration failed.
  EXPECT_CALL(driver_event_handler_, OnDriverFailure(_, _));
  wireguard_tools_exit_callback_.Run(1);

  // Checks that the config file has been deleted.
  EXPECT_FALSE(base::PathExists(config_file_path_));
}

TEST_F(WireGuardDriverTest, PropertyStoreAndConfigFile) {
  std::string contents;
  Error err;
  InitializePropertyStore();

  // Save & load should not lose any information.
  const std::string kStorageId = "wireguard-test";
  driver_->Save(&fake_store_, kStorageId, /*save_credentials=*/true);
  ResetDriver();
  driver_->Load(&fake_store_, kStorageId);
  InvokeConnectAsyncKernel();
  InvokeLinkReady();
  CHECK(base::ReadFileToString(config_file_path_, &contents));
  EXPECT_EQ(contents, kExpectedConfigFileContents);
  driver_->Disconnect();

  // Checks reading properties. Private keys and preshared keys should not be
  // readable.
  KeyValueStore provider;
  EXPECT_TRUE(property_store_->GetKeyValueStoreProperty(kProviderProperty,
                                                        &provider, &err));
  EXPECT_FALSE(provider.Contains<std::string>(kWireGuardPrivateKey));
  EXPECT_TRUE(provider.Contains<std::string>(kWireGuardPublicKey));
  EXPECT_EQ(provider.Get<Stringmaps>(kWireGuardPeers),
            (Stringmaps{
                {{kWireGuardPeerPublicKey, "public-key-1"},
                 {kWireGuardPeerPersistentKeepalive, "10"},
                 {kWireGuardPeerEndpoint, "10.0.1.1:12345"},
                 {kWireGuardPeerAllowedIPs, "192.168.1.2/32,192.168.2.0/24"}},
                {{kWireGuardPeerPublicKey, "public-key-2"},
                 {kWireGuardPeerPersistentKeepalive, ""},
                 {kWireGuardPeerEndpoint, "10.0.1.2:12345"},
                 {kWireGuardPeerAllowedIPs, "192.168.1.2/32,192.168.3.0/24"}},
            }));

  // Setting peers without touching PresharedKey property should leave it
  // unchanged.
  property_store_->SetStringmapsProperty(
      kWireGuardPeers,
      {
          {{kWireGuardPeerPublicKey, "public-key-1"},
           {kWireGuardPeerPersistentKeepalive, "10"},
           {kWireGuardPeerEndpoint, "10.0.1.1:12345"},
           {kWireGuardPeerAllowedIPs, "192.168.1.2/32,192.168.2.0/24"}},
          {{kWireGuardPeerPublicKey, "public-key-2"},
           {kWireGuardPeerEndpoint, "10.0.1.2:12345"},
           {kWireGuardPeerAllowedIPs, "192.168.1.2/32,192.168.3.0/24"}},
      },
      &err);
  InvokeConnectAsyncKernel();
  InvokeLinkReady();
  CHECK(base::ReadFileToString(config_file_path_, &contents));
  EXPECT_EQ(contents, kExpectedConfigFileContents);
  driver_->Disconnect();

  // Setting peers with an empty PublicKey property should be rejected and the
  // current peers will not be modified.
  property_store_->SetStringmapsProperty(
      kWireGuardPeers,
      {
          {{kWireGuardPeerPublicKey, "public-key-1"},
           {kWireGuardPeerPersistentKeepalive, "10"},
           {kWireGuardPeerEndpoint, "10.0.1.1:12345"},
           {kWireGuardPeerAllowedIPs, "192.168.1.2/32,192.168.2.0/24"}},
          {{kWireGuardPeerPublicKey, ""},
           {kWireGuardPeerEndpoint, "10.0.1.2:12345"},
           {kWireGuardPeerAllowedIPs, "192.168.1.2/32,192.168.3.0/24"}},
      },
      &err);
  EXPECT_TRUE(err.IsFailure());
  InvokeConnectAsyncKernel();
  InvokeLinkReady();
  CHECK(base::ReadFileToString(config_file_path_, &contents));
  EXPECT_EQ(contents, kExpectedConfigFileContents);
  driver_->Disconnect();

  // Setting peers with a duplicated PublicKey property should be rejected and
  // the current peers will not be modified.
  property_store_->SetStringmapsProperty(
      kWireGuardPeers,
      {
          {{kWireGuardPeerPublicKey, "public-key-1"},
           {kWireGuardPeerPersistentKeepalive, "10"},
           {kWireGuardPeerEndpoint, "10.0.1.1:12345"},
           {kWireGuardPeerAllowedIPs, "192.168.1.2/32,192.168.2.0/24"}},
          {{kWireGuardPeerPublicKey, "public-key-1"},
           {kWireGuardPeerEndpoint, "10.0.1.2:12345"},
           {kWireGuardPeerAllowedIPs, "192.168.1.2/32,192.168.3.0/24"}},
      },
      &err);
  EXPECT_TRUE(err.IsFailure());
  InvokeConnectAsyncKernel();
  InvokeLinkReady();
  CHECK(base::ReadFileToString(config_file_path_, &contents));
  EXPECT_EQ(contents, kExpectedConfigFileContents);
  driver_->Disconnect();

  // Setting peers with an empty PresharedKey property should clear the
  // PresharedKey of that peer.
  property_store_->SetStringmapsProperty(
      kWireGuardPeers,
      {
          {{kWireGuardPeerPublicKey, "public-key-1"},
           {kWireGuardPeerPresharedKey, ""},
           {kWireGuardPeerPersistentKeepalive, "10"},
           {kWireGuardPeerEndpoint, "10.0.1.1:12345"},
           {kWireGuardPeerAllowedIPs, "192.168.1.2/32,192.168.2.0/24"}},
          {{kWireGuardPeerPublicKey, "public-key-2"},
           {kWireGuardPeerEndpoint, "10.0.1.2:12345"},
           {kWireGuardPeerAllowedIPs, "192.168.1.2/32,192.168.3.0/24"}},
      },
      &err);
  InvokeConnectAsyncKernel();
  InvokeLinkReady();
  CHECK(base::ReadFileToString(config_file_path_, &contents));
  EXPECT_THAT(contents, Not(HasSubstr("PresharedKey=")));
}

TEST_F(WireGuardDriverTest, UnloadCredentials) {
  InitializePropertyStore();
  driver_->UnloadCredentials();
  const auto args = driver_->const_args();
  EXPECT_FALSE(args->Contains<std::string>(kWireGuardPrivateKey));
  EXPECT_EQ(driver_test_peer_->peers(),
            (Stringmaps{
                {{kWireGuardPeerPublicKey, "public-key-1"},
                 {kWireGuardPeerPresharedKey, ""},
                 {kWireGuardPeerPersistentKeepalive, "10"},
                 {kWireGuardPeerEndpoint, "10.0.1.1:12345"},
                 {kWireGuardPeerAllowedIPs, "192.168.1.2/32,192.168.2.0/24"}},
                {{kWireGuardPeerPublicKey, "public-key-2"},
                 {kWireGuardPeerPresharedKey, ""},
                 {kWireGuardPeerEndpoint, "10.0.1.2:12345"},
                 {kWireGuardPeerAllowedIPs, "192.168.1.2/32,192.168.3.0/24"}},
            }));
}

TEST_F(WireGuardDriverTest, KeyPairGeneration) {
  Error err;
  const std::string kStorageId = "wireguard-test";

  auto assert_pubkey_is = [&](const std::string& key) {
    KeyValueStore provider;
    ASSERT_TRUE(property_store_->GetKeyValueStoreProperty(kProviderProperty,
                                                          &provider, &err));
    ASSERT_EQ(provider.Get<std::string>(kWireGuardPublicKey), key);
  };
  auto assert_pubkey_not_empty = [&]() {
    KeyValueStore provider;
    ASSERT_TRUE(property_store_->GetKeyValueStoreProperty(kProviderProperty,
                                                          &provider, &err));
    ASSERT_FALSE(provider.Get<std::string>(kWireGuardPublicKey).empty());
  };

  driver_->Save(&fake_store_, kStorageId, /*save_credentials=*/true);
  assert_pubkey_not_empty();

  ResetDriver();
  driver_->Load(&fake_store_, kStorageId);
  assert_pubkey_not_empty();

  property_store_->SetStringProperty(kWireGuardPrivateKey, kPrivateKey1, &err);
  driver_->Save(&fake_store_, kStorageId, /*save_credentials=*/true);
  assert_pubkey_is(kPrivateKey1);

  property_store_->SetStringProperty(kWireGuardPrivateKey, "", &err);
  driver_->Save(&fake_store_, kStorageId, /*save_credentials=*/true);
  assert_pubkey_not_empty();

  property_store_->SetStringProperty(kWireGuardPrivateKey, kPrivateKey2, &err);
  driver_->Save(&fake_store_, kStorageId, /*save_credentials=*/true);
  assert_pubkey_is(kPrivateKey2);
}

TEST_F(WireGuardDriverTest, SpawnWireGuardProcessFailed) {
  driver_->ConnectAsync(&driver_event_handler_);
  EXPECT_CALL(device_info_, CreateWireGuardInterface(kIfName, _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(process_manager_,
              StartProcessInMinijail(_, _, _, _, _, _, _, _, _, _))
      .WillOnce(Return(-1));
  EXPECT_CALL(driver_event_handler_, OnDriverFailure(_, _));
  dispatcher_.DispatchPendingEvents();
}

TEST_F(WireGuardDriverTest, WireGuardProcessExitedUnexpectedly) {
  InvokeConnectAsyncUserspace();
  EXPECT_CALL(driver_event_handler_, OnDriverFailure(_, _));
  wireguard_exit_callback_.Run(1);
}

// Checks interface cleanup on timeout.
TEST_F(WireGuardDriverTest, OnConnectTimeout) {
  InitializePropertyStore();

  // Link is not created.
  InvokeConnectAsyncKernel();
  EXPECT_CALL(driver_event_handler_, OnDriverFailure(_, _));
  driver_->OnConnectTimeout();

  // Link is created by kernel.
  InvokeConnectAsyncKernel();
  InvokeLinkReady();
  EXPECT_CALL(driver_event_handler_, OnDriverFailure(_, _));
  EXPECT_CALL(device_info_, DeleteInterface(kIfIndex));
  driver_->OnConnectTimeout();

  // Link is created by userspace process.
  InvokeConnectAsyncUserspace();
  InvokeLinkReady();
  EXPECT_CALL(driver_event_handler_, OnDriverFailure(_, _));
  EXPECT_CALL(process_manager_, StopProcess(kWireGuardPid));
  driver_->OnConnectTimeout();
}

// Checks interface cleanup on disconnect before connected. Different with
// OnConnectTimeout, disconnect will not trigger an OnDriverFailure callback.
TEST_F(WireGuardDriverTest, DisconnectBeforeConnected) {
  InitializePropertyStore();

  // Link is not created.
  InvokeConnectAsyncKernel();
  driver_->Disconnect();

  // Link is created by kernel.
  InvokeConnectAsyncKernel();
  InvokeLinkReady();
  EXPECT_CALL(device_info_, DeleteInterface(kIfIndex));
  driver_->Disconnect();

  // Link is created by userspace process.
  InvokeConnectAsyncUserspace();
  InvokeLinkReady();
  EXPECT_CALL(process_manager_, StopProcess(kWireGuardPid));
  driver_->OnConnectTimeout();
}

}  // namespace
}  // namespace shill
