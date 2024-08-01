// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/dhcpcd_proxy.h"

#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/functional/callback_helpers.h>
#include <base/test/task_environment.h>
#include <chromeos/net-base/ip_address.h>
#include <chromeos/net-base/mock_process_manager.h>
#include <chromeos/net-base/process_manager.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/network/dhcp_client_proxy.h"
#include "shill/network/dhcpv4_config.h"

namespace shill {
namespace {

using testing::_;
using testing::Field;
using testing::Return;

// The mock client of the DHCPCDProxy.
class MockClient : public DHCPClientProxy::EventHandler {
 public:
  MOCK_METHOD(void,
              OnDHCPEvent,
              (DHCPClientProxy::EventReason,
               const net_base::NetworkConfig&,
               const DHCPv4Config::Data&),
              (override));
  MOCK_METHOD(void, OnProcessExited, (int, int), (override));
};

class DHCPCDProxyFactoryTest : public testing::Test {
 public:
  void SetUp() override {
    EXPECT_TRUE(temp_dir_.CreateUniqueTempDir());
    root_path_ = temp_dir_.GetPath();

    proxy_factory_ =
        std::make_unique<DHCPCDProxyFactory>(&mock_process_manager_);
    proxy_factory_->set_root_for_testing(root_path_);
  }

  std::unique_ptr<DHCPClientProxy> CreateProxySync(
      int expected_pid, std::string_view interface = "wlan0") {
    const DHCPClientProxy::Options options = {};

    // When creating a proxy, the proxy factory should create the dhcpcd process
    // in minijail.
    EXPECT_CALL(mock_process_manager_, StartProcessInMinijail)
        .WillOnce(Return(expected_pid));
    EXPECT_CALL(mock_process_manager_, UpdateExitCallback(expected_pid, _))
        .WillOnce(
            [this](pid_t pid,
                   net_base::MockProcessManager::ExitCallback new_callback) {
              process_exit_cb_ = std::move(new_callback);
              return true;
            });

    std::unique_ptr<DHCPClientProxy> proxy =
        proxy_factory_->Create(interface, Technology::kWiFi, options, &client_);
    EXPECT_NE(proxy, nullptr);
    EXPECT_TRUE(proxy->IsReady());
    return proxy;
  }

  void CreateTempFileInRoot(std::string_view file) {
    const base::FilePath path_in_root = root_path_.Append(file);
    EXPECT_TRUE(base::CreateDirectory(path_in_root.DirName()));
    EXPECT_EQ(0, base::WriteFile(path_in_root, "", 0));
  }

  bool FileExistsInRoot(std::string_view file) {
    return base::PathExists(root_path_.Append(file));
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir temp_dir_;
  base::FilePath root_path_;

  net_base::MockProcessManager mock_process_manager_;
  net_base::MockProcessManager::ExitCallback process_exit_cb_;
  std::unique_ptr<DHCPCDProxyFactory> proxy_factory_;

  MockClient client_;
};

TEST_F(DHCPCDProxyFactoryTest, DhcpcdArguments) {
  constexpr int kPid = 4;

  const std::vector<std::tuple<net_base::IPFamily, DHCPClientProxy::Options,
                               std::vector<std::string>>>
      kExpectedArgs = {
          {net_base::IPFamily::kIPv4,
           {},
           {"-B", "-i", "chromeos", "-q", "-o", "captive_portal_uri", "-o",
            "wpad_url", "--clientid", "--nodelay", "--noconfigure", "-4", "-A",
            "-K", "wlan0"}},
          {net_base::IPFamily::kIPv4,
           {.hostname = "my_hostname"},
           {"-B", "-i", "chromeos", "-q", "-o", "captive_portal_uri", "-o",
            "wpad_url", "--clientid", "--nodelay", "--noconfigure", "-4", "-A",
            "-K", "-h", "my_hostname", "wlan0"}},
          {net_base::IPFamily::kIPv4,
           {.use_rfc_8925 = true},
           {"-B", "-i", "chromeos", "-q", "-o", "captive_portal_uri", "-o",
            "wpad_url", "--clientid", "--nodelay", "--noconfigure", "-4", "-A",
            "-K", "-o", "ipv6_only_preferred", "wlan0"}},
          {net_base::IPFamily::kIPv4,
           {.apply_dscp = true},
           {"-B", "-i", "chromeos", "-q", "-o", "captive_portal_uri", "-o",
            "wpad_url", "--clientid", "--nodelay", "--noconfigure", "-4", "-A",
            "-K", "--apply_dscp", "wlan0"}},
          {net_base::IPFamily::kIPv6,
           {},
           {"-B", "-i", "chromeos", "-q", "-o", "captive_portal_uri", "-o",
            "wpad_url", "--clientid", "--nodelay", "--noconfigure", "-6",
            "--noipv6rs", "--ia_pd", "wlan0"}},
          {net_base::IPFamily::kIPv6,
           {.hostname = "my_hostname"},
           {"-B", "-i", "chromeos", "-q", "-o", "captive_portal_uri", "-o",
            "wpad_url", "--clientid", "--nodelay", "--noconfigure", "-6",
            "--noipv6rs", "--ia_pd", "-h", "my_hostname", "wlan0"}},
          {net_base::IPFamily::kIPv6,
           {.use_rfc_8925 = true /* should be ignored*/},
           {"-B", "-i", "chromeos", "-q", "-o", "captive_portal_uri", "-o",
            "wpad_url", "--clientid", "--nodelay", "--noconfigure", "-6",
            "--noipv6rs", "--ia_pd", "wlan0"}},
          {net_base::IPFamily::kIPv6,
           {.apply_dscp = true},
           {"-B", "-i", "chromeos", "-q", "-o", "captive_portal_uri", "-o",
            "wpad_url", "--clientid", "--nodelay", "--noconfigure", "-6",
            "--noipv6rs", "--ia_pd", "--apply_dscp", "wlan0"}},
      };
  for (const auto& [family, options, dhcpcd_args] : kExpectedArgs) {
    // When creating a proxy, the proxy factory should create
    // the dhcpcd process in minijail.
    EXPECT_CALL(mock_process_manager_,
                StartProcessInMinijail(_, base::FilePath("/sbin/dhcpcd"),
                                       dhcpcd_args, _, _, _))
        .WillOnce(Return(kPid));
    EXPECT_CALL(mock_process_manager_, UpdateExitCallback(kPid, _))
        .WillOnce(
            [this](pid_t pid,
                   net_base::MockProcessManager::ExitCallback new_callback) {
              process_exit_cb_ = std::move(new_callback);
              return true;
            });

    proxy_factory_->Create("wlan0", Technology::kWiFi, options, &client_,
                           family);
  }
}

TEST_F(DHCPCDProxyFactoryTest, CreateAndDestroyProxy) {
  constexpr int kPid = 4;

  std::unique_ptr<DHCPClientProxy> proxy = CreateProxySync(kPid);

  // The dhcpcd process should be terminated when the proxy is destroyed.
  EXPECT_CALL(mock_process_manager_, StopProcessAndBlock(kPid));
  proxy.reset();
}

TEST_F(DHCPCDProxyFactoryTest, KillProcessWithPendingRequest) {
  constexpr int kPid = 4;

  std::unique_ptr<DHCPClientProxy> proxy = CreateProxySync(kPid);

  // The dhcpcd process should be killed when the factory is destroyed.
  EXPECT_CALL(mock_process_manager_, StopProcessAndBlock(kPid));
  proxy_factory_.reset();
}

TEST_F(DHCPCDProxyFactoryTest, CreateMultipleProxies) {
  constexpr int kPid1 = 4;
  constexpr int kPid2 = 6;

  std::unique_ptr<DHCPClientProxy> proxy1 = CreateProxySync(kPid1);
  std::unique_ptr<DHCPClientProxy> proxy2 = CreateProxySync(kPid2);

  // The dhcpcd process should be terminated when the proxy is destroyed.
  EXPECT_CALL(mock_process_manager_, StopProcessAndBlock(kPid1));
  EXPECT_CALL(mock_process_manager_, StopProcessAndBlock(kPid2));
  proxy_factory_.reset();
}

TEST_F(DHCPCDProxyFactoryTest, ProcessExited) {
  constexpr int kPid = 4;
  constexpr std::string_view kInterface = "wlan1";
  constexpr std::string_view kPidFile = "var/run/dhcpcd/dhcpcd-wlan1-4.pid";
  constexpr std::string_view kLeaseFile = "var/lib/dhcpcd/wlan1.lease";
  constexpr int kExitStatus = 3;

  std::unique_ptr<DHCPClientProxy> proxy = CreateProxySync(kPid, kInterface);

  CreateTempFileInRoot(kPidFile);
  CreateTempFileInRoot(kLeaseFile);
  EXPECT_TRUE(FileExistsInRoot(kPidFile));
  EXPECT_TRUE(FileExistsInRoot(kLeaseFile));

  // When ProcessManager triggers the process exit callback, the factory should
  // notify the client by EventHandler::OnProcessExited().
  EXPECT_CALL(client_, OnProcessExited(kPid, kExitStatus));
  // The process is already exited, we should not stop it again.
  EXPECT_CALL(mock_process_manager_, StopProcessAndBlock(kPid)).Times(0);

  std::move(process_exit_cb_).Run(kExitStatus);

  // After the process is exited, the pid file and the lease file should be
  // deleted.
  EXPECT_FALSE(FileExistsInRoot(kPidFile));
  EXPECT_FALSE(FileExistsInRoot(kLeaseFile));
}

TEST_F(DHCPCDProxyFactoryTest, DeleteEphemeralLeaseAndPidFile) {
  constexpr int kPid = 4;
  constexpr std::string_view kInterface = "wlan0";
  constexpr std::string_view kPidFile = "var/run/dhcpcd/dhcpcd-wlan0-4.pid";
  constexpr std::string_view kLeaseFile = "var/lib/dhcpcd/wlan0.lease";
  const DHCPClientProxy::Options options = {};

  std::unique_ptr<DHCPClientProxy> proxy = CreateProxySync(kPid, kInterface);

  CreateTempFileInRoot(kPidFile);
  CreateTempFileInRoot(kLeaseFile);
  EXPECT_TRUE(FileExistsInRoot(kPidFile));
  EXPECT_TRUE(FileExistsInRoot(kLeaseFile));

  // After the proxy is destroyed, the pid file and the lease file should be
  // deleted.
  proxy.reset();
  EXPECT_FALSE(FileExistsInRoot(kPidFile));
  EXPECT_FALSE(FileExistsInRoot(kLeaseFile));
}

TEST_F(DHCPCDProxyFactoryTest, Rebind) {
  constexpr int kPid = 4;
  constexpr std::string_view kInterface = "wlan0";
  const std::vector<std::string> kArgs = {"-4", "--noconfigure", "--rebind",
                                          "wlan0"};
  constexpr int kRebindPid = 5;

  std::unique_ptr<DHCPClientProxy> proxy = CreateProxySync(kPid, kInterface);

  EXPECT_CALL(
      mock_process_manager_,
      StartProcessInMinijail(
          _, base::FilePath("/sbin/dhcpcd"), kArgs, _,
          Field(&net_base::ProcessManager::MinijailOptions::capmask, 0), _))
      .WillOnce(Return(kRebindPid));
  EXPECT_TRUE(proxy->Rebind());
  testing::Mock::VerifyAndClearExpectations(&mock_process_manager_);

  EXPECT_CALL(
      mock_process_manager_,
      StartProcessInMinijail(
          _, base::FilePath("/sbin/dhcpcd"), kArgs, _,
          Field(&net_base::ProcessManager::MinijailOptions::capmask, 0), _))
      .WillOnce(Return(net_base::ProcessManager::kInvalidPID));
  EXPECT_FALSE(proxy->Rebind());
}

TEST_F(DHCPCDProxyFactoryTest, Release) {
  constexpr int kPid = 4;
  constexpr std::string_view kInterface = "wlan0";
  const std::vector<std::string> kArgs = {"-4", "--noconfigure", "--release",
                                          "wlan0"};
  constexpr int kReleasePid = 5;

  std::unique_ptr<DHCPClientProxy> proxy = CreateProxySync(kPid, kInterface);

  EXPECT_CALL(
      mock_process_manager_,
      StartProcessInMinijail(
          _, base::FilePath("/sbin/dhcpcd"), kArgs, _,
          Field(&net_base::ProcessManager::MinijailOptions::capmask, 0), _))
      .WillOnce(Return(kReleasePid));
  EXPECT_TRUE(proxy->Release());
  testing::Mock::VerifyAndClearExpectations(&mock_process_manager_);

  EXPECT_CALL(
      mock_process_manager_,
      StartProcessInMinijail(
          _, base::FilePath("/sbin/dhcpcd"), kArgs, _,
          Field(&net_base::ProcessManager::MinijailOptions::capmask, 0), _))
      .WillOnce(Return(net_base::ProcessManager::kInvalidPID));
  EXPECT_FALSE(proxy->Release());
}

TEST_F(DHCPCDProxyFactoryTest, OnDHCPEvent) {
  constexpr int kPid = 4;
  constexpr std::string_view kInterface = "wlan0";

  const std::map<std::string, std::string> configuration = {
      {DHCPv4Config::kConfigurationKeyPid, "4"},
      {DHCPv4Config::kConfigurationKeyInterface, "wlan0"},
      {DHCPv4Config::kConfigurationKeyReason, "BOUND"},
      {DHCPv4Config::kConfigurationKeyIPAddress, "4.3.2.1"},
  };

  std::unique_ptr<DHCPClientProxy> proxy = CreateProxySync(kPid, kInterface);
  EXPECT_CALL(client_, OnDHCPEvent(DHCPClientProxy::EventReason::kBound, _, _));

  proxy_factory_->OnDHCPEvent(configuration);
}

TEST(DHCPCDProxyTest, ConvertConfigurationToKeyValueStoreIPAddress) {
  const std::map<std::string, std::string> configuration = {
      {DHCPv4Config::kConfigurationKeyIPAddress, "4.3.2.1"},
  };
  const KeyValueStore store =
      DHCPCDProxy::ConvertConfigurationToKeyValueStore(configuration);

  EXPECT_EQ(store.Get<uint32_t>(DHCPv4Config::kConfigurationKeyIPAddress),
            0x01020304);
}

TEST(DHCPCDProxyTest, ConvertConfigurationToKeyValueStoreSubnetCIDR) {
  const std::map<std::string, std::string> configuration = {
      {DHCPv4Config::kConfigurationKeySubnetCIDR, "16"},
  };
  const KeyValueStore store =
      DHCPCDProxy::ConvertConfigurationToKeyValueStore(configuration);

  EXPECT_EQ(store.Get<uint8_t>(DHCPv4Config::kConfigurationKeySubnetCIDR), 16);
}

TEST(DHCPCDProxyTest, ConvertConfigurationToKeyValueStoreBroadcastAddress) {
  const std::map<std::string, std::string> configuration = {
      {DHCPv4Config::kConfigurationKeyBroadcastAddress, "4.3.2.1"},
  };
  const KeyValueStore store =
      DHCPCDProxy::ConvertConfigurationToKeyValueStore(configuration);

  EXPECT_EQ(
      store.Get<uint32_t>(DHCPv4Config::kConfigurationKeyBroadcastAddress),
      0x01020304);
}

TEST(DHCPCDProxyTest, ConvertConfigurationToKeyValueStoreRoutersAndDNS) {
  for (const auto key : {DHCPv4Config::kConfigurationKeyRouters,
                         DHCPv4Config::kConfigurationKeyDNS}) {
    const std::map<std::string, std::string> configuration = {
        {key, "4.3.2.1 8.7.6.5"},
    };
    const KeyValueStore store =
        DHCPCDProxy::ConvertConfigurationToKeyValueStore(configuration);

    const std::vector<uint32_t> expect{0x01020304, 0x05060708};
    EXPECT_EQ(store.Get<std::vector<uint32_t>>(key), expect);
  }
}

TEST(DHCPCDProxyTest, ConvertConfigurationToKeyValueStoreString) {
  for (const auto key : {
           DHCPv4Config::kConfigurationKeyDomainName,
           DHCPv4Config::kConfigurationKeyCaptivePortalUri,
           DHCPv4Config::kConfigurationKeyClasslessStaticRoutes,
           DHCPv4Config::kConfigurationKeyWebProxyAutoDiscoveryUrl,
       }) {
    const std::map<std::string, std::string> configuration = {
        {key, "TEST_STRING"},
    };
    const KeyValueStore store =
        DHCPCDProxy::ConvertConfigurationToKeyValueStore(configuration);

    EXPECT_EQ(store.Get<std::string>(key), "TEST_STRING");
  }
}

TEST(DHCPCDProxyTest, ConvertConfigurationToKeyValueStoreDomainSearch) {
  const std::map<std::string, std::string> configuration = {
      {DHCPv4Config::kConfigurationKeyDomainSearch, "domain1 domain2"},
  };
  const KeyValueStore store =
      DHCPCDProxy::ConvertConfigurationToKeyValueStore(configuration);

  const std::vector<std::string> expect{"domain1", "domain2"};
  EXPECT_EQ(store.Get<std::vector<std::string>>(
                DHCPv4Config::kConfigurationKeyDomainSearch),
            expect);
}

TEST(DHCPCDProxyTest, ConvertConfigurationToKeyValueStoreMTU) {
  const std::map<std::string, std::string> configuration = {
      {DHCPv4Config::kConfigurationKeyMTU, "1450"},
  };
  const KeyValueStore store =
      DHCPCDProxy::ConvertConfigurationToKeyValueStore(configuration);

  EXPECT_EQ(store.Get<uint16_t>(DHCPv4Config::kConfigurationKeyMTU), 1450);
}

TEST(DHCPCDProxyTest,
     ConvertConfigurationToKeyValueStoreendorEncapsulatedOptions) {
  const std::map<std::string, std::string> configuration = {
      {DHCPv4Config::kConfigurationKeyVendorEncapsulatedOptions, "13579ABC"},
  };
  const KeyValueStore store =
      DHCPCDProxy::ConvertConfigurationToKeyValueStore(configuration);

  const std::vector<uint8_t> expect{0x13, 0x57, 0x9A, 0xBC};
  EXPECT_EQ(store.Get<std::vector<uint8_t>>(
                DHCPv4Config::kConfigurationKeyVendorEncapsulatedOptions),
            expect);
}

TEST(DHCPCDProxyTest, ConvertConfigurationToKeyValueStoreendorLeaseTime) {
  const std::map<std::string, std::string> configuration = {
      {DHCPv4Config::kConfigurationKeyLeaseTime, "300"},
  };
  const KeyValueStore store =
      DHCPCDProxy::ConvertConfigurationToKeyValueStore(configuration);

  EXPECT_EQ(store.Get<uint32_t>(DHCPv4Config::kConfigurationKeyLeaseTime), 300);
}

}  // namespace
}  // namespace shill
