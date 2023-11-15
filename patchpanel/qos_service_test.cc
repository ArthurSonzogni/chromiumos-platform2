// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/qos_service.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/containers/span.h>
#include <base/memory/ptr_util.h>
#include <base/memory/weak_ptr.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <net-base/dns_client.h>
#include <net-base/ipv4_address.h>

#include "patchpanel/conntrack_monitor.h"
#include "patchpanel/mock_conntrack_monitor.h"
#include "patchpanel/mock_datapath.h"
#include "patchpanel/mock_process_runner.h"
#include "patchpanel/routing_service.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {
namespace {

using ::testing::_;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::Mock;
using ::testing::Return;
using ::testing::StrEq;
using ::testing::StrictMock;
using DNSClient = net_base::DNSClient;
using IPAddress = net_base::IPAddress;

constexpr char kIPAddress1[] = "8.8.8.8";
constexpr char kIPAddress2[] = "8.8.8.4";
constexpr int kPort1 = 10000;
constexpr int kPort2 = 20000;
constexpr ConntrackMonitor::EventType kConntrackEvents[] = {
    ConntrackMonitor::EventType::kNew};

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

// Verifies the interactions between QoSService and Datapath when feature on the
// events of feature enable/disable and device change events.
TEST(QoSServiceTest, EnableDisableQoSFeature) {
  using Device = ShillClient::Device;
  const Device kEth0 = {
      .type = Device::Type::kEthernet,
      .ifname = "eth0",
  };
  const Device kEth1 = {
      .type = Device::Type::kEthernet,
      .ifname = "eth1",
  };
  const Device kWlan0 = {
      .type = Device::Type::kWifi,
      .ifname = "wlan0",
  };
  const Device kWlan1 = {
      .type = Device::Type::kWifi,
      .ifname = "wlan1",
  };

  StrictMock<MockDatapath> datapath;
  MockConntrackMonitor conntrack_monitor;
  QoSService qos_svc(&datapath, &conntrack_monitor);

  // No interaction with Datapath before feature is enabled.
  qos_svc.OnPhysicalDeviceAdded(kEth0);
  qos_svc.OnPhysicalDeviceAdded(kWlan0);
  qos_svc.OnPhysicalDeviceRemoved(kWlan0);
  qos_svc.OnPhysicalDeviceAdded(kWlan0);
  Mock::VerifyAndClearExpectations(&datapath);

  // On feature enabled, the detection chain should be enabled, and the DSCP
  // marking chain for the existing interface should be enabled.
  EXPECT_CALL(datapath, EnableQoSDetection);
  EXPECT_CALL(datapath, EnableQoSApplyingDSCP("wlan0"));
  qos_svc.Enable();
  Mock::VerifyAndClearExpectations(&datapath);

  // No interaction with Datapath on uninteresting or already-tracked
  // interfaces.
  qos_svc.OnPhysicalDeviceAdded(kEth1);
  qos_svc.OnPhysicalDeviceAdded(kWlan0);
  Mock::VerifyAndClearExpectations(&datapath);

  // Device change events on interesting interfaces.
  EXPECT_CALL(datapath, DisableQoSApplyingDSCP("wlan0"));
  EXPECT_CALL(datapath, EnableQoSApplyingDSCP("wlan1"));
  qos_svc.OnPhysicalDeviceRemoved(kWlan0);
  qos_svc.OnPhysicalDeviceAdded(kWlan1);
  Mock::VerifyAndClearExpectations(&datapath);

  // On feature disabled.
  EXPECT_CALL(datapath, DisableQoSDetection);
  EXPECT_CALL(datapath, DisableQoSApplyingDSCP("wlan1"));
  qos_svc.Disable();
  Mock::VerifyAndClearExpectations(&datapath);

  // Device change events when disabled, and then enable again.
  qos_svc.OnPhysicalDeviceRemoved(kWlan1);
  qos_svc.OnPhysicalDeviceAdded(kWlan0);
  EXPECT_CALL(datapath, EnableQoSDetection);
  EXPECT_CALL(datapath, EnableQoSApplyingDSCP("wlan0"));
  qos_svc.Enable();
  Mock::VerifyAndClearExpectations(&datapath);
}

// Verifies that ProcessSocketConnectionEvent behaves correctly when
// feature on the events of feature enable/disable.
TEST(QoSServiceTest, ProcessSocketConnectionEvent) {
  auto datapath = MockDatapath();
  auto runner = std::make_unique<MockProcessRunner>();
  auto runner_ptr = runner.get();
  MockConntrackMonitor conntrack_monitor;
  QoSService qos_svc(&datapath, /*dns_client_factory=*/nullptr,
                     std::move(runner), &conntrack_monitor);
  std::unique_ptr<patchpanel::SocketConnectionEvent> open_msg =
      CreateOpenSocketConnectionEvent();
  std::unique_ptr<patchpanel::SocketConnectionEvent> close_msg =
      CreateCloseSocketConnectionEvent();

  // No interaction with ProcessRunner before feature is enabled.
  EXPECT_CALL(*runner_ptr, conntrack("-U", _, _)).Times(0);
  qos_svc.ProcessSocketConnectionEvent(*open_msg);
  Mock::VerifyAndClearExpectations(runner_ptr);

  // After feature is enabled, process socket connection event will trigger
  // corresponding connmark update.
  qos_svc.Enable();
  std::vector<std::string> argv = {
      "-p",      "TCP",
      "-s",      kIPAddress1,
      "-d",      kIPAddress2,
      "--sport", std::to_string(kPort1),
      "--dport", std::to_string(kPort2),
      "-m",      QoSFwmarkWithMask(QoSCategory::kRealTimeInteractive)};
  EXPECT_CALL(*runner_ptr, conntrack("-U", ElementsAreArray(argv), _));

  qos_svc.ProcessSocketConnectionEvent(*open_msg);
  argv = {"-p",      "TCP",
          "-s",      kIPAddress1,
          "-d",      kIPAddress2,
          "--sport", std::to_string(kPort1),
          "--dport", std::to_string(kPort2),
          "-m",      QoSFwmarkWithMask(QoSCategory::kDefault)};
  EXPECT_CALL(*runner_ptr, conntrack("-U", ElementsAreArray(argv), _));
  qos_svc.ProcessSocketConnectionEvent(*close_msg);
  Mock::VerifyAndClearExpectations(runner_ptr);

  // No interaction with process runner after feature is disabled.
  EXPECT_CALL(*runner_ptr, conntrack("-U", _, _)).Times(0);
  qos_svc.Disable();
  qos_svc.ProcessSocketConnectionEvent(*open_msg);
  Mock::VerifyAndClearExpectations(runner_ptr);
}

// QoSService should start DNS queries for each valid hostname in DoHProviders,
// and Datapath will be notified when all DNS queries finished.
TEST(QoSServiceTest, UpdateDoHProviders) {
  MockDatapath mock_datapath;
  FakeDNSClientFactory* dns_factory = new FakeDNSClientFactory();
  MockConntrackMonitor conntrack_monitor;
  QoSService svc(&mock_datapath, base::WrapUnique(dns_factory),
                 /*minijailed_process_runner=*/nullptr, &conntrack_monitor);

  // Update DoH list with 2 valid entries. There should be 4 DNS requests in
  // total.
  const ShillClient::DoHProviders doh_list = {
      "https://url-a",
      "https://url-b",
      "http://want-https",
      "no-https-prefix",
      "",  // check that no crash for empty string
  };

  EXPECT_CALL(*dns_factory,
              Resolve(net_base::IPFamily::kIPv4, "url-a", _, _, _));
  EXPECT_CALL(*dns_factory,
              Resolve(net_base::IPFamily::kIPv6, "url-a", _, _, _));
  EXPECT_CALL(*dns_factory,
              Resolve(net_base::IPFamily::kIPv4, "url-b", _, _, _));
  EXPECT_CALL(*dns_factory,
              Resolve(net_base::IPFamily::kIPv6, "url-b", _, _, _));

  svc.UpdateDoHProviders(doh_list);

  ASSERT_EQ(2, dns_factory->ipv4_callbacks().size());
  ASSERT_EQ(2, dns_factory->ipv6_callbacks().size());

  // Datapath methods should only be invoked when we get all the callbacks.
  const auto kIPv4Addr1 = IPAddress::CreateFromString("1.2.3.4").value();
  const auto kIPv4Addr2 = IPAddress::CreateFromString("1.2.3.5").value();
  const auto kIPv6Addr1 = IPAddress::CreateFromString("fd00::1").value();
  const auto kIPv6Addr2 = IPAddress::CreateFromString("fd00::2").value();

  EXPECT_CALL(mock_datapath, UpdateDoHProvidersForQoS).Times(0);
  dns_factory->TriggerIPv4Callback(DNSClient::Result({kIPv4Addr1}));
  dns_factory->TriggerIPv4Callback(DNSClient::Result({kIPv4Addr1, kIPv4Addr2}));
  dns_factory->TriggerIPv6Callback(DNSClient::Result({kIPv6Addr1, kIPv6Addr2}));

  EXPECT_CALL(mock_datapath,
              UpdateDoHProvidersForQoS(
                  IpFamily::kIPv4,
                  std::vector<net_base::IPAddress>{kIPv4Addr1, kIPv4Addr2}));
  EXPECT_CALL(mock_datapath,
              UpdateDoHProvidersForQoS(
                  IpFamily::kIPv6,
                  std::vector<net_base::IPAddress>{kIPv6Addr1, kIPv6Addr2}));
  // Trigger the last callback with an error.
  dns_factory->TriggerIPv6Callback(
      DNSClient::Result(base::unexpected(DNSClient::Error::kRefused)));
}

// Datapath should be notified when DoH provider list is empty.
TEST(QoSServiceTest, UpdateDoHProvidersEmptyInput) {
  MockDatapath mock_datapath;
  FakeDNSClientFactory* dns_factory = new FakeDNSClientFactory();
  MockConntrackMonitor conntrack_monitor;
  QoSService svc(&mock_datapath, base::WrapUnique(dns_factory),
                 /*minijailed_process_runner=*/nullptr, &conntrack_monitor);

  EXPECT_CALL(mock_datapath,
              UpdateDoHProvidersForQoS(IpFamily::kIPv4,
                                       std::vector<net_base::IPAddress>{}));
  EXPECT_CALL(mock_datapath,
              UpdateDoHProvidersForQoS(IpFamily::kIPv6,
                                       std::vector<net_base::IPAddress>{}));

  svc.UpdateDoHProviders({});
}

// Datapath should be notified when the resolved result is empty.
TEST(QoSServiceTest, UpdateDoHProvidersEmptyResolveResult) {
  MockDatapath mock_datapath;
  FakeDNSClientFactory* dns_factory = new FakeDNSClientFactory();
  MockConntrackMonitor conntrack_monitor;
  QoSService svc(&mock_datapath, base::WrapUnique(dns_factory),
                 /*minijailed_process_runner=*/nullptr, &conntrack_monitor);

  svc.UpdateDoHProviders({"https://url-a", "https://url-b"});

  EXPECT_CALL(mock_datapath,
              UpdateDoHProvidersForQoS(IpFamily::kIPv4,
                                       std::vector<net_base::IPAddress>{}));
  EXPECT_CALL(mock_datapath,
              UpdateDoHProvidersForQoS(IpFamily::kIPv6,
                                       std::vector<net_base::IPAddress>{}));
  dns_factory->TriggerIPv4Callback(
      DNSClient::Result(base::unexpected(DNSClient::Error::kNoData)));
  dns_factory->TriggerIPv4Callback(
      DNSClient::Result(base::unexpected(DNSClient::Error::kRefused)));
  dns_factory->TriggerIPv6Callback(
      DNSClient::Result(base::unexpected(DNSClient::Error::kBadQuery)));
  dns_factory->TriggerIPv6Callback(
      DNSClient::Result(base::unexpected(DNSClient::Error::kBadResp)));
}

// If the DoH provider list is updated again when we are still processing the
// previous update, all the ongoing DNS requests should be cancelled.
TEST(QoSServiceTest, UpdateDoHProvidersInvalidateOngoingQueries) {
  MockDatapath mock_datapath;
  FakeDNSClientFactory* dns_factory = new FakeDNSClientFactory();
  MockConntrackMonitor conntrack_monitor;
  QoSService svc(&mock_datapath, base::WrapUnique(dns_factory),
                 /*minijailed_process_runner=*/nullptr, &conntrack_monitor);

  svc.UpdateDoHProviders({"https://url-a", "https://url-b"});

  auto client_ptrs = dns_factory->GetWeakPtrsToExistingClients();
  ASSERT_EQ(client_ptrs.size(), 4);

  svc.UpdateDoHProviders({"https://url-d", "https://url-e"});
  for (const auto ptr : client_ptrs) {
    EXPECT_TRUE(ptr.WasInvalidated());
  }
}

TEST(QoSServiceTest, OnBorealisVMStarted) {
  MockDatapath mock_datapath;
  FakeDNSClientFactory* dns_factory = new FakeDNSClientFactory();
  MockConntrackMonitor conntrack_monitor;
  QoSService svc(&mock_datapath, base::WrapUnique(dns_factory),
                 /*minijailed_process_runner=*/nullptr, &conntrack_monitor);

  const auto borealis_ipv4_subnet =
      net_base::IPv4CIDR::CreateFromCIDRString("100.115.93.0/29").value();
  auto ipv4_subnet =
      std::make_unique<Subnet>(borealis_ipv4_subnet, base::DoNothing());
  CrostiniService::CrostiniDevice borealis_device(
      CrostiniService::VMType::kBorealis, "vmtap1", {}, std::move(ipv4_subnet),
      nullptr);

  EXPECT_CALL(mock_datapath, AddBorealisQoSRule("vmtap1"));

  svc.OnBorealisVMStarted("vmtap1");
}

TEST(QoSServiceTest, OnBorealisVMStopped) {
  MockDatapath mock_datapath;
  FakeDNSClientFactory* dns_factory = new FakeDNSClientFactory();
  MockConntrackMonitor conntrack_monitor;
  QoSService svc(&mock_datapath, base::WrapUnique(dns_factory),
                 /*minijailed_process_runner=*/nullptr, &conntrack_monitor);

  const auto borealis_ipv4_subnet =
      *net_base::IPv4CIDR::CreateFromCIDRString("100.115.93.0/29");
  auto ipv4_subnet =
      std::make_unique<Subnet>(borealis_ipv4_subnet, base::DoNothing());
  CrostiniService::CrostiniDevice borealis_device(
      CrostiniService::VMType::kBorealis, "vmtap1", {}, std::move(ipv4_subnet),
      nullptr);

  EXPECT_CALL(mock_datapath, RemoveBorealisQoSRule("vmtap1"));

  svc.OnBorealisVMStopped("vmtap1");
}

// QoSService should add conntrack listener once when enabled from disabled
// state.
TEST(QoSServiceTest, AddListener) {
  MockDatapath mock_datapath;
  StrictMock<MockConntrackMonitor> conntrack_monitor;
  QoSService qos_svc(&mock_datapath, &conntrack_monitor);

  // Listener will be added when QoSService is enabled.
  EXPECT_CALL(conntrack_monitor,
              AddListener(ElementsAreArray(kConntrackEvents), _));
  qos_svc.Enable();
  Mock::VerifyAndClearExpectations(&conntrack_monitor);

  qos_svc.Enable();
  Mock::VerifyAndClearExpectations(&conntrack_monitor);

  // Listener will be added again when QoSService is re-enabled.
  EXPECT_CALL(conntrack_monitor,
              AddListener(ElementsAreArray(kConntrackEvents), _));
  qos_svc.Disable();
  qos_svc.Enable();
  Mock::VerifyAndClearExpectations(&conntrack_monitor);
}

// When failing to update connmark for UDP socket event, QoSService should
// try updating again when getting conntrack event from ConntrackMonitor.
TEST(QoSServiceTest, HandleConntrackEvent) {
  MockDatapath mock_datapath;
  auto runner = std::make_unique<MockProcessRunner>();
  auto runner_ptr = runner.get();
  std::unique_ptr<patchpanel::SocketConnectionEvent> open_msg =
      CreateOpenSocketConnectionEvent();

  MockConntrackMonitor monitor;
  monitor.Start(kConntrackEvents);
  QoSService qos_svc(&mock_datapath, /*dns_client_factory=*/nullptr,
                     std::move(runner), &monitor);
  qos_svc.Enable();

  // When updating connmark for TCP sockets fails, it will not be updated again
  // even getting conntrack event from ConntrackMonitor.
  std::vector<std::string> argv = {
      "-p",      "TCP",
      "-s",      kIPAddress1,
      "-d",      kIPAddress2,
      "--sport", std::to_string(kPort1),
      "--dport", std::to_string(kPort2),
      "-m",      QoSFwmarkWithMask(QoSCategory::kRealTimeInteractive)};
  EXPECT_CALL(*runner_ptr, conntrack("-U", ElementsAreArray(argv), _))
      .WillOnce(Return(-1));
  qos_svc.ProcessSocketConnectionEvent(*open_msg);
  Mock::VerifyAndClearExpectations(runner_ptr);
  EXPECT_CALL(*runner_ptr, conntrack("-U", _, _)).Times(0);
  const ConntrackMonitor::Event kTCPEvent = ConntrackMonitor::Event{
      .src = *net_base::IPAddress::CreateFromString(kIPAddress1),
      .dst = (*net_base::IPAddress::CreateFromString(kIPAddress2)),
      .sport = kPort1,
      .dport = kPort2,
      .proto = IPPROTO_TCP,
      .type = ConntrackMonitor::EventType::kNew};
  monitor.DispatchEventForTesting(kTCPEvent);
  Mock::VerifyAndClearExpectations(runner_ptr);

  // When updating connmark for UDP sockets fails, it will be updated again
  // when getting event notifications from ConntrackMonitor.
  argv = {"-p",      "UDP",
          "-s",      kIPAddress1,
          "-d",      kIPAddress2,
          "--sport", std::to_string(kPort1),
          "--dport", std::to_string(kPort2),
          "-m",      QoSFwmarkWithMask(QoSCategory::kRealTimeInteractive)};
  EXPECT_CALL(*runner_ptr, conntrack("-U", ElementsAreArray(argv), _))
      .WillOnce(Return(-1));

  open_msg->set_proto(patchpanel::SocketConnectionEvent::IpProtocol::
                          SocketConnectionEvent_IpProtocol_UDP);
  qos_svc.ProcessSocketConnectionEvent(*open_msg);
  Mock::VerifyAndClearExpectations(runner_ptr);
  EXPECT_CALL(*runner_ptr, conntrack("-U", ElementsAreArray(argv), _));
  const ConntrackMonitor::Event kUDPEvent = ConntrackMonitor::Event{
      .src = *net_base::IPAddress::CreateFromString(kIPAddress1),
      .dst = (*net_base::IPAddress::CreateFromString(kIPAddress2)),
      .sport = kPort1,
      .dport = kPort2,
      .proto = IPPROTO_UDP,
      .type = ConntrackMonitor::EventType::kNew};
  monitor.DispatchEventForTesting(kUDPEvent);
  Mock::VerifyAndClearExpectations(runner_ptr);

  // Update will not be triggered when getting another identical event
  // notification from ConntrackMonitor.
  EXPECT_CALL(*runner_ptr, conntrack("-U", _, _)).Times(0);
  monitor.DispatchEventForTesting(kUDPEvent);
  Mock::VerifyAndClearExpectations(runner_ptr);
}
}  // namespace
}  // namespace patchpanel
