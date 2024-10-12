// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/qos_service.h"

#include <cstddef>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include <base/containers/span.h>
#include <base/memory/ptr_util.h>
#include <base/memory/weak_ptr.h>
#include <base/test/task_environment.h>
#include <chromeos/net-base/dns_client.h>
#include <chromeos/net-base/ipv4_address.h>
#include <chromeos/net-base/technology.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "patchpanel/connmark_updater.h"
#include "patchpanel/fake_process_runner.h"
#include "patchpanel/mock_connmark_updater.h"
#include "patchpanel/mock_conntrack_monitor.h"
#include "patchpanel/mock_datapath.h"
#include "patchpanel/noop_system.h"
#include "patchpanel/routing_service.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {
namespace {

using ::testing::_;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrEq;
using DNSClient = net_base::DNSClient;
using IPAddress = net_base::IPAddress;

constexpr char kIPAddress1[] = "8.8.8.8";
constexpr char kIPAddress2[] = "8.8.8.4";
constexpr int kPort1 = 10000;
constexpr int kPort2 = 20000;

const net_base::IPAddress kIPv4DNS =
    net_base::IPAddress::CreateFromString("8.8.8.8").value();
const net_base::IPAddress kIPv6DNS =
    net_base::IPAddress::CreateFromString("fd00::53").value();

std::unique_ptr<patchpanel::SocketConnectionEvent>
CreateOpenSocketConnectionEvent() {
  std::unique_ptr<patchpanel::SocketConnectionEvent> msg =
      std::make_unique<patchpanel::SocketConnectionEvent>();
  net_base::IPv4Address src_addr =
      *net_base::IPv4Address::CreateFromString(kIPAddress1);
  msg->set_saddr(src_addr.ToByteString());
  net_base::IPv4Address dst_addr =
      *net_base::IPv4Address::CreateFromString(kIPAddress2);
  msg->set_daddr(dst_addr.ToByteString());

  msg->set_sport(kPort1);
  msg->set_dport(kPort2);
  msg->set_proto(patchpanel::SocketConnectionEvent::IpProtocol::
                     SocketConnectionEvent_IpProtocol_TCP);
  msg->set_category(patchpanel::SocketConnectionEvent::QosCategory::
                        SocketConnectionEvent_QosCategory_REALTIME_INTERACTIVE);
  msg->set_event(patchpanel::SocketConnectionEvent::SocketEvent::
                     SocketConnectionEvent_SocketEvent_OPEN);
  return msg;
}

std::unique_ptr<patchpanel::SocketConnectionEvent>
CreateCloseSocketConnectionEvent() {
  std::unique_ptr<patchpanel::SocketConnectionEvent> msg =
      CreateOpenSocketConnectionEvent();
  msg->set_event(patchpanel::SocketConnectionEvent::SocketEvent::
                     SocketConnectionEvent_SocketEvent_CLOSE);
  return msg;
}

// The fake client doesn't need to do anything. WeakPtrFactory is for querying
// whether the object is still valid in the test.
class FakeDNSClient : public DNSClient {
 public:
  base::WeakPtr<FakeDNSClient> AsWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 private:
  base::WeakPtrFactory<FakeDNSClient> weak_factory_{this};
};

class FakeDNSClientFactory : public net_base::DNSClientFactory {
 public:
  using Callbacks = std::vector<DNSClient::Callback>;

  FakeDNSClientFactory() {
    ON_CALL(*this, Resolve)
        .WillByDefault([this](net_base::IPFamily family,
                              std::string_view hostname,
                              DNSClient::Callback callback,
                              const DNSClient::Options& options,
                              net_base::AresInterface* ares_interface) {
          switch (family) {
            case net_base::IPFamily::kIPv4:
              ipv4_callbacks_.emplace_back(std::move(callback));
              break;
            case net_base::IPFamily::kIPv6:
              ipv6_callbacks_.emplace_back(std::move(callback));
              break;
          }
          auto ret = std::make_unique<FakeDNSClient>();
          clients_.push_back(ret->AsWeakPtr());
          return ret;
        });
  }

  MOCK_METHOD(std::unique_ptr<DNSClient>,
              Resolve,
              (net_base::IPFamily family,
               std::string_view hostname,
               DNSClient::Callback callback,
               const DNSClient::Options& options,
               net_base::AresInterface* ares_interface),
              (override));

  void TriggerIPv4Callback(const DNSClient::Result& result) {
    std::move(ipv4_callbacks_.back()).Run(result);
    ipv4_callbacks_.pop_back();
  }
  void TriggerIPv6Callback(const DNSClient::Result& result) {
    std::move(ipv6_callbacks_.back()).Run(result);
    ipv6_callbacks_.pop_back();
  }

  // Returns a copy of weak pointers to existing clients.
  std::vector<base::WeakPtr<FakeDNSClient>> GetWeakPtrsToExistingClients()
      const {
    return clients_;
  }

  const Callbacks& ipv4_callbacks() const { return ipv4_callbacks_; }
  const Callbacks& ipv6_callbacks() const { return ipv6_callbacks_; }

 private:
  std::vector<DNSClient::Callback> ipv4_callbacks_;
  std::vector<DNSClient::Callback> ipv6_callbacks_;
  std::vector<base::WeakPtr<FakeDNSClient>> clients_;
};

// Notes:
// - ShillClient is only used for accessing states in QoSService so fake is
//   better than mock in this test;
// - The FakeShillClient in fake_shill_client.h is too complicated to use so
//   create our own one here. Add a suffix in the name to avoid conflicts in any
//   case.
class FakeShillClientForQoS : public ShillClient {
 public:
  FakeShillClientForQoS() : ShillClient(nullptr, nullptr) {}
  ~FakeShillClientForQoS() = default;

  const Device* GetDeviceByShillDeviceName(
      const std::string& shill_device_interface_property) const override {
    for (const auto& device : devices_) {
      if (device.ifname == shill_device_interface_property) {
        return &device;
      }
    }
    return nullptr;
  }

  void set_devices(const std::vector<Device>& devices) { devices_ = devices; }

  void set_doh_providers(const DoHProviders& value) {
    set_doh_providers_for_testing(value);
  }

 private:
  std::vector<Device> devices_;
};

ShillClient::Device CreateShillDevice(
    std::string_view ifname,
    bool is_connected,
    const std::vector<net_base::IPAddress> dns_servers) {
  ShillClient::Device device;
  device.ifname = ifname;
  device.technology = net_base::Technology::kWiFi;
  device.network_config.dns_servers = dns_servers;
  if (is_connected) {
    // The value does not matter, just to make sure Device::IsConnected()
    // returns true.
    device.network_config.ipv4_address =
        net_base::IPv4CIDR::CreateFromCIDRString("1.2.3.4/32").value();
  }
  return device;
}

class QoSServiceTest : public testing::Test {
 protected:
  QoSServiceTest()
      : datapath_(&process_runner_, &system_),
        dns_factory_(new FakeDNSClientFactory()),
        qos_svc_(&datapath_,
                 &conntrack_monitor_,
                 &shill_client_,
                 base::WrapUnique(dns_factory_)) {}

  // Note that this needs to be initialized at first, since the ctors of other
  // members may rely on it (e.g., FileDescriptorWatcher).
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::IO};

  FakeProcessRunner process_runner_;
  NoopSystem system_;
  NiceMock<MockDatapath> datapath_;
  FakeDNSClientFactory* dns_factory_;  // Owned by |qos_svc_|.
  FakeShillClientForQoS shill_client_;
  NiceMock<MockConntrackMonitor> conntrack_monitor_;
  QoSService qos_svc_;
};

// Verifies the interactions between QoSService and Datapath when feature on the
// events of feature enable/disable and device change events.
TEST_F(QoSServiceTest, EnableDisableQoSFeature) {
  const ShillClient::Device kEth0 = {
      .technology = net_base::Technology::kEthernet,
      .ifname = "eth0",
  };
  const ShillClient::Device kEth1 = {
      .technology = net_base::Technology::kEthernet,
      .ifname = "eth1",
  };
  const ShillClient::Device kWlan0 = {
      .technology = net_base::Technology::kWiFi,
      .ifname = "wlan0",
  };
  const ShillClient::Device kWlan1 = {
      .technology = net_base::Technology::kWiFi,
      .ifname = "wlan1",
  };

  // No interaction with Datapath before feature is enabled.
  EXPECT_CALL(datapath_, EnableQoSDetection).Times(0);
  EXPECT_CALL(datapath_, EnableQoSApplyingDSCP).Times(0);
  qos_svc_.OnPhysicalDeviceAdded(kEth0);
  qos_svc_.OnPhysicalDeviceAdded(kWlan0);
  qos_svc_.OnPhysicalDeviceRemoved(kWlan0);
  qos_svc_.OnPhysicalDeviceAdded(kWlan0);
  Mock::VerifyAndClearExpectations(&datapath_);

  // On feature enabled, the detection chain should be enabled, and the DSCP
  // marking chain for the existing interface should be enabled.
  EXPECT_CALL(datapath_, EnableQoSDetection);
  EXPECT_CALL(datapath_, EnableQoSApplyingDSCP("wlan0"));
  qos_svc_.Enable();
  Mock::VerifyAndClearExpectations(&datapath_);

  // No interaction with Datapath on uninteresting or already-tracked
  // interfaces.
  EXPECT_CALL(datapath_, EnableQoSDetection).Times(0);
  EXPECT_CALL(datapath_, EnableQoSApplyingDSCP).Times(0);
  qos_svc_.OnPhysicalDeviceAdded(kEth1);
  qos_svc_.OnPhysicalDeviceAdded(kWlan0);
  Mock::VerifyAndClearExpectations(&datapath_);

  // Device change events on interesting interfaces.
  EXPECT_CALL(datapath_, DisableQoSApplyingDSCP("wlan0"));
  EXPECT_CALL(datapath_, EnableQoSApplyingDSCP("wlan1"));
  qos_svc_.OnPhysicalDeviceRemoved(kWlan0);
  qos_svc_.OnPhysicalDeviceAdded(kWlan1);
  Mock::VerifyAndClearExpectations(&datapath_);

  // On feature disabled.
  EXPECT_CALL(datapath_, DisableQoSDetection);
  EXPECT_CALL(datapath_, DisableQoSApplyingDSCP("wlan1"));
  qos_svc_.Disable();
  Mock::VerifyAndClearExpectations(&datapath_);

  // Device change events when disabled, and then enable again.
  qos_svc_.OnPhysicalDeviceRemoved(kWlan1);
  qos_svc_.OnPhysicalDeviceAdded(kWlan0);
  EXPECT_CALL(datapath_, EnableQoSDetection);
  EXPECT_CALL(datapath_, EnableQoSApplyingDSCP("wlan0"));
  qos_svc_.Enable();
  Mock::VerifyAndClearExpectations(&datapath_);
}

// Verifies that ProcessSocketConnectionEvent behaves correctly when
// feature on the events of feature enable/disable.
TEST_F(QoSServiceTest, ProcessSocketConnectionEvent) {
  auto updater = std::make_unique<MockConnmarkUpdater>(&conntrack_monitor_);
  auto updater_ptr = updater.get();
  qos_svc_.SetConnmarkUpdaterForTesting(std::move(updater));
  std::unique_ptr<patchpanel::SocketConnectionEvent> open_msg =
      CreateOpenSocketConnectionEvent();
  std::unique_ptr<patchpanel::SocketConnectionEvent> close_msg =
      CreateCloseSocketConnectionEvent();

  // No interaction with ConnmarkUpdater before feature is enabled.
  EXPECT_CALL(*updater_ptr, UpdateConnmark).Times(0);
  qos_svc_.ProcessSocketConnectionEvent(*open_msg);
  Mock::VerifyAndClearExpectations(updater_ptr);

  // After feature is enabled, process socket connection event will trigger
  // corresponding connmark update.
  qos_svc_.Enable();
  // When enabling QoS service, a new ConnmarkUpdater will be assigned, so
  // assign mock ConnmarkUpdater to QoS service again.
  updater = std::make_unique<MockConnmarkUpdater>(&conntrack_monitor_);
  updater_ptr = updater.get();
  qos_svc_.SetConnmarkUpdaterForTesting(std::move(updater));
  auto tcp_conn = ConnmarkUpdater::Conntrack5Tuple{
      .src_addr = *(net_base::IPAddress::CreateFromString(kIPAddress1)),
      .dst_addr = *(net_base::IPAddress::CreateFromString(kIPAddress2)),
      .sport = static_cast<uint16_t>(kPort1),
      .dport = static_cast<uint16_t>(kPort2),
      .proto = ConnmarkUpdater::IPProtocol::kTCP};
  EXPECT_CALL(
      *updater_ptr,
      UpdateConnmark(Eq(tcp_conn),
                     Fwmark::FromQoSCategory(QoSCategory::kRealTimeInteractive),
                     kFwmarkQoSCategoryMask));
  qos_svc_.ProcessSocketConnectionEvent(*open_msg);
  EXPECT_CALL(*updater_ptr,
              UpdateConnmark(Eq(tcp_conn),
                             Fwmark::FromQoSCategory(QoSCategory::kDefault),
                             kFwmarkQoSCategoryMask));
  qos_svc_.ProcessSocketConnectionEvent(*close_msg);
  Mock::VerifyAndClearExpectations(updater_ptr);
  // No interaction with ConnmarkUpdater after feature is disabled.
  EXPECT_CALL(*updater_ptr, UpdateConnmark).Times(0);
  qos_svc_.Disable();
  qos_svc_.ProcessSocketConnectionEvent(*open_msg);
  Mock::VerifyAndClearExpectations(updater_ptr);
}

// QoSService should start DNS queries for each valid hostname in DoHProviders,
// and Datapath will be notified when all DNS queries finished.
TEST_F(QoSServiceTest, UpdateDoHProviders) {
  const auto wlan0_device =
      CreateShillDevice("wlan0", /*is_connected=*/true, {kIPv4DNS});
  shill_client_.set_devices({wlan0_device});
  qos_svc_.OnPhysicalDeviceAdded(wlan0_device);

  // Update DoH list with 2 valid entries. There should be 4 DNS requests in
  // total.
  const ShillClient::DoHProviders doh_list = {
      "https://url-a",
      "https://url-b",
      "http://want-https",
      "no-https-prefix",
      "",  // check that no crash for empty string
  };

  EXPECT_CALL(*dns_factory_,
              Resolve(net_base::IPFamily::kIPv4, "url-a", _, _, _));
  EXPECT_CALL(*dns_factory_,
              Resolve(net_base::IPFamily::kIPv6, "url-a", _, _, _));
  EXPECT_CALL(*dns_factory_,
              Resolve(net_base::IPFamily::kIPv4, "url-b", _, _, _));
  EXPECT_CALL(*dns_factory_,
              Resolve(net_base::IPFamily::kIPv6, "url-b", _, _, _));

  shill_client_.set_doh_providers(doh_list);
  qos_svc_.OnDoHProvidersChanged();

  ASSERT_EQ(2, dns_factory_->ipv4_callbacks().size());
  ASSERT_EQ(2, dns_factory_->ipv6_callbacks().size());

  // Datapath methods should only be invoked when we get all the callbacks.
  const auto kIPv4Addr1 = IPAddress::CreateFromString("1.2.3.4").value();
  const auto kIPv4Addr2 = IPAddress::CreateFromString("1.2.3.5").value();
  const auto kIPv6Addr1 = IPAddress::CreateFromString("fd00::1").value();
  const auto kIPv6Addr2 = IPAddress::CreateFromString("fd00::2").value();

  EXPECT_CALL(datapath_, UpdateDoHProvidersForQoS).Times(0);
  dns_factory_->TriggerIPv4Callback(DNSClient::Result({kIPv4Addr1}));
  dns_factory_->TriggerIPv4Callback(
      DNSClient::Result({kIPv4Addr1, kIPv4Addr2}));
  dns_factory_->TriggerIPv6Callback(
      DNSClient::Result({kIPv6Addr1, kIPv6Addr2}));

  EXPECT_CALL(datapath_, UpdateDoHProvidersForQoS(
                             IpFamily::kIPv4, std::vector<net_base::IPAddress>{
                                                  kIPv4Addr1, kIPv4Addr2}));
  EXPECT_CALL(datapath_, UpdateDoHProvidersForQoS(
                             IpFamily::kIPv6, std::vector<net_base::IPAddress>{
                                                  kIPv6Addr1, kIPv6Addr2}));
  // Trigger the last callback with an error.
  dns_factory_->TriggerIPv6Callback(
      DNSClient::Result(base::unexpected(DNSClient::Error::kRefused)));
}

// Datapath should be notified when DoH provider list is empty.
TEST_F(QoSServiceTest, UpdateDoHProvidersEmptyInput) {
  const auto wlan0_device =
      CreateShillDevice("wlan0", /*is_connected=*/true, {kIPv4DNS});
  shill_client_.set_devices({wlan0_device});
  qos_svc_.OnPhysicalDeviceAdded(wlan0_device);

  EXPECT_CALL(datapath_,
              UpdateDoHProvidersForQoS(IpFamily::kIPv4,
                                       std::vector<net_base::IPAddress>{}));
  EXPECT_CALL(datapath_,
              UpdateDoHProvidersForQoS(IpFamily::kIPv6,
                                       std::vector<net_base::IPAddress>{}));

  shill_client_.set_doh_providers({});
  qos_svc_.OnDoHProvidersChanged();
}

// Datapath should be notified when the resolved result is empty.
TEST_F(QoSServiceTest, UpdateDoHProvidersEmptyResolveResult) {
  const auto wlan0_device =
      CreateShillDevice("wlan0", /*is_connected=*/true, {kIPv4DNS});
  shill_client_.set_devices({wlan0_device});
  qos_svc_.OnPhysicalDeviceAdded(wlan0_device);

  shill_client_.set_doh_providers({"https://url-a", "https://url-b"});
  qos_svc_.OnDoHProvidersChanged();

  EXPECT_CALL(datapath_,
              UpdateDoHProvidersForQoS(IpFamily::kIPv4,
                                       std::vector<net_base::IPAddress>{}));
  EXPECT_CALL(datapath_,
              UpdateDoHProvidersForQoS(IpFamily::kIPv6,
                                       std::vector<net_base::IPAddress>{}));
  dns_factory_->TriggerIPv4Callback(
      DNSClient::Result(base::unexpected(DNSClient::Error::kNoData)));
  dns_factory_->TriggerIPv4Callback(
      DNSClient::Result(base::unexpected(DNSClient::Error::kRefused)));
  dns_factory_->TriggerIPv6Callback(
      DNSClient::Result(base::unexpected(DNSClient::Error::kBadQuery)));
  dns_factory_->TriggerIPv6Callback(
      DNSClient::Result(base::unexpected(DNSClient::Error::kBadResp)));
}

// If the DoH provider list is updated again when we are still processing the
// previous update, all the ongoing DNS requests should be cancelled.
TEST_F(QoSServiceTest, UpdateDoHProvidersInvalidateOngoingQueries) {
  const auto wlan0_device =
      CreateShillDevice("wlan0", /*is_connected=*/true, {kIPv4DNS});
  shill_client_.set_devices({wlan0_device});
  qos_svc_.OnPhysicalDeviceAdded(wlan0_device);

  shill_client_.set_doh_providers({"https://url-a", "https://url-b"});
  qos_svc_.OnDoHProvidersChanged();

  auto client_ptrs = dns_factory_->GetWeakPtrsToExistingClients();
  ASSERT_EQ(client_ptrs.size(), 4);

  // Nothing will happen if the DoH providers are not changed.
  qos_svc_.OnDoHProvidersChanged();
  for (const auto ptr : client_ptrs) {
    EXPECT_FALSE(ptr.WasInvalidated());
  }

  // Ongoing tasks should be cancelled if DoH providers are changed.
  shill_client_.set_doh_providers({"https://url-d", "https://url-e"});
  qos_svc_.OnDoHProvidersChanged();
  for (const auto ptr : client_ptrs) {
    EXPECT_TRUE(ptr.WasInvalidated());
  }
}

// Verify that DNS clients are started properly on IPConfig change events. The
// interaction with Datapath is verified in the above tests and thus skipped in
// this test.
TEST_F(QoSServiceTest, UpdateDoHProvidersIPConfigChanged) {
  shill_client_.set_doh_providers({"https://url-a"});
  qos_svc_.OnDoHProvidersChanged();

  const auto wlan0_connected_dns_v4 =
      CreateShillDevice("wlan0", /*is_connected=*/true, {kIPv4DNS});
  const auto wlan0_not_connected_dns_v4 =
      CreateShillDevice("wlan0", /*is_connected=*/false, {kIPv4DNS});
  const auto wlan0_connected_dns_dual =
      CreateShillDevice("wlan0", /*is_connected=*/true, {kIPv4DNS, kIPv6DNS});

  // Connected but the device is not tracked.
  EXPECT_CALL(*dns_factory_, Resolve).Times(0);
  qos_svc_.OnIPConfigChanged(wlan0_connected_dns_v4);
  Mock::VerifyAndClearExpectations(dns_factory_);

  // Device is tracked but not connected.
  EXPECT_CALL(*dns_factory_, Resolve).Times(0);
  qos_svc_.OnPhysicalDeviceAdded(wlan0_not_connected_dns_v4);
  qos_svc_.OnIPConfigChanged(wlan0_not_connected_dns_v4);
  Mock::VerifyAndClearExpectations(dns_factory_);

  // Device is tracked and connected, DNS clients should be created (1 DoH
  // provider x 2 IP family).
  EXPECT_CALL(*dns_factory_, Resolve).Times(2);
  qos_svc_.OnIPConfigChanged(wlan0_connected_dns_v4);
  Mock::VerifyAndClearExpectations(dns_factory_);

  // DNS servers changed, DNS clients should be created again (1 DoH provider x
  // 2 IP family x 2 dns servers)
  EXPECT_CALL(*dns_factory_, Resolve).Times(4);
  qos_svc_.OnIPConfigChanged(wlan0_connected_dns_dual);
  Mock::VerifyAndClearExpectations(dns_factory_);

  // DNS servers not changed, DNS clients should no be created.
  EXPECT_CALL(*dns_factory_, Resolve).Times(0);
  qos_svc_.OnIPConfigChanged(wlan0_connected_dns_dual);
  Mock::VerifyAndClearExpectations(dns_factory_);
}

TEST_F(QoSServiceTest, OnBorealisVMStarted) {
  const auto borealis_ipv4_subnet =
      net_base::IPv4CIDR::CreateFromCIDRString("100.115.93.0/29").value();
  auto ipv4_subnet =
      std::make_unique<Subnet>(borealis_ipv4_subnet, base::DoNothing());
  CrostiniService::CrostiniDevice borealis_device(
      CrostiniService::VMType::kBorealis, "vmtap1", std::move(ipv4_subnet),
      nullptr);

  EXPECT_CALL(datapath_, AddBorealisQoSRule("vmtap1"));

  qos_svc_.OnBorealisVMStarted("vmtap1");
}

TEST_F(QoSServiceTest, OnBorealisVMStopped) {
  const auto borealis_ipv4_subnet =
      *net_base::IPv4CIDR::CreateFromCIDRString("100.115.93.0/29");
  auto ipv4_subnet =
      std::make_unique<Subnet>(borealis_ipv4_subnet, base::DoNothing());
  CrostiniService::CrostiniDevice borealis_device(
      CrostiniService::VMType::kBorealis, "vmtap1", std::move(ipv4_subnet),
      nullptr);

  EXPECT_CALL(datapath_, RemoveBorealisQoSRule("vmtap1"));

  qos_svc_.OnBorealisVMStopped("vmtap1");
}

// QoSService can handle socket connection events correctly. When socket
// connection event is received, call ConnmarkUpdater to handle the update
// task.
TEST_F(QoSServiceTest, HandleSocketConnectionEvent) {
  std::unique_ptr<patchpanel::SocketConnectionEvent> open_msg =
      CreateOpenSocketConnectionEvent();

  qos_svc_.Enable();
  auto updater = std::make_unique<MockConnmarkUpdater>(&conntrack_monitor_);
  auto updater_ptr = updater.get();
  qos_svc_.SetConnmarkUpdaterForTesting(std::move(updater));

  // When notified of TCP socket event, call ConnmarkUpdater for connmark
  // update for this connection.
  auto tcp_conn = ConnmarkUpdater::Conntrack5Tuple{
      .src_addr = *(net_base::IPAddress::CreateFromString(kIPAddress1)),
      .dst_addr = *(net_base::IPAddress::CreateFromString(kIPAddress2)),
      .sport = static_cast<uint16_t>(kPort1),
      .dport = static_cast<uint16_t>(kPort2),
      .proto = ConnmarkUpdater::IPProtocol::kTCP};
  EXPECT_CALL(
      *updater_ptr,
      UpdateConnmark(Eq(tcp_conn),
                     Fwmark::FromQoSCategory(QoSCategory::kRealTimeInteractive),
                     kFwmarkQoSCategoryMask));
  qos_svc_.ProcessSocketConnectionEvent(*open_msg);
  Mock::VerifyAndClearExpectations(updater_ptr);

  // When notified of UDP socket event, call ConnmarkUpdater for connmark
  // update for this connection.
  auto udp_conn = ConnmarkUpdater::Conntrack5Tuple{
      .src_addr = *(net_base::IPAddress::CreateFromString(kIPAddress1)),
      .dst_addr = *(net_base::IPAddress::CreateFromString(kIPAddress2)),
      .sport = static_cast<uint16_t>(kPort1),
      .dport = static_cast<uint16_t>(kPort2),
      .proto = ConnmarkUpdater::IPProtocol::kUDP};
  EXPECT_CALL(
      *updater_ptr,
      UpdateConnmark(Eq(udp_conn),
                     Fwmark::FromQoSCategory(QoSCategory::kRealTimeInteractive),
                     kFwmarkQoSCategoryMask));
  open_msg->set_proto(patchpanel::SocketConnectionEvent::IpProtocol::
                          SocketConnectionEvent_IpProtocol_UDP);
  qos_svc_.ProcessSocketConnectionEvent(*open_msg);
  Mock::VerifyAndClearExpectations(updater_ptr);
}

}  // namespace
}  // namespace patchpanel
