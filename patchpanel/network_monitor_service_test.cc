// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/network_monitor_service.h"

#include <linux/rtnetlink.h>

#include <memory>
#include <set>

#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/test/task_environment.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/net-base/byte_utils.h>
#include <chromeos/net-base/ip_address.h>
#include <chromeos/net-base/mock_rtnl_handler.h>
#include <gtest/gtest.h>

#include "patchpanel/fake_shill_client.h"

namespace patchpanel {

namespace {
constexpr int kTestInterfaceIndex = 1;
constexpr char kTestInterfaceName[] = "wlan0";

using ::testing::Eq;

MATCHER(IsNeighborDumpMessage, "") {
  if (!(arg->type() == net_base::RTNLMessage::kTypeNeighbor &&
        arg->flags() == (NLM_F_REQUEST | NLM_F_DUMP) &&
        arg->mode() == net_base::RTNLMessage::kModeGet &&
        arg->interface_index() == kTestInterfaceIndex)) {
    return false;
  }

  return true;
}

MATCHER_P(IsNeighborProbeMessage, address, "") {
  if (!(arg->type() == net_base::RTNLMessage::kTypeNeighbor &&
        arg->flags() == (NLM_F_REQUEST | NLM_F_REPLACE) &&
        arg->mode() == net_base::RTNLMessage::kModeAdd &&
        arg->neighbor_status().state == NUD_PROBE &&
        arg->interface_index() == kTestInterfaceIndex &&
        arg->HasAttribute(NDA_DST))) {
    return false;
  }

  const auto addr_bytes = arg->GetAttribute(NDA_DST);
  const auto msg_address = net_base::IPAddress::CreateFromBytes(addr_bytes);
  CHECK(msg_address.has_value());
  const auto expected_address = net_base::IPAddress::CreateFromString(address);
  CHECK(expected_address.has_value());
  return msg_address == expected_address;
}

// Helper class for testing. Similar to mock class but only allowed one
// expectation set at the same time.
class FakeNeighborReachabilityEventHandler {
 public:
  ~FakeNeighborReachabilityEventHandler() {
    if (!enabled_) {
      return;
    }

    EXPECT_FALSE(expectation_set_)
        << "Expected " << ExpectationToString() << ", but not called.";
  }

  void Enable() { enabled_ = true; }

  void Disable() {
    EXPECT_TRUE(enabled_);
    EXPECT_FALSE(expectation_set_)
        << "Expected " << ExpectationToString() << ", but not called.";
    enabled_ = false;
  }

  void Expect(int ifindex,
              const std::string& ip_addr,
              NeighborLinkMonitor::NeighborRole role,
              NeighborReachabilityEventSignal::EventType event_type) {
    EXPECT_TRUE(enabled_);
    EXPECT_FALSE(expectation_set_)
        << "Expected " << ExpectationToString() << ", but not called.";
    expectation_set_ = true;
    expected_ifindex_ = ifindex;
    expected_ip_addr_ = ip_addr;
    expected_role_ = role;
    expected_event_type_ = event_type;
  }

  void Run(int ifindex,
           const net_base::IPAddress& ip_addr,
           NeighborLinkMonitor::NeighborRole role,
           NeighborReachabilityEventSignal::EventType event_type) {
    if (!enabled_) {
      return;
    }

    const std::string callback_str =
        CallbackToString(ifindex, ip_addr.ToString(), role, event_type);
    EXPECT_TRUE(expectation_set_)
        << callback_str << " called, but not expected.";
    expectation_set_ = false;
    EXPECT_TRUE((expected_ifindex_ == ifindex) &&
                (expected_ip_addr_ == ip_addr.ToString()) &&
                (expected_role_ == role) &&
                (expected_event_type_ == event_type))
        << "Expected " << ExpectationToString() << ", but got " << callback_str;
  }

 private:
  static std::string CallbackToString(
      int ifindex,
      const std::string& ip_addr,
      NeighborLinkMonitor::NeighborRole role,
      NeighborReachabilityEventSignal::EventType event_type) {
    return base::StrCat(
        {"{ ifindex: ", base::NumberToString(ifindex), ", ip_addr: ", ip_addr,
         ", role: ", NeighborLinkMonitor::NeighborRoleToString(role),
         ", type: ", base::NumberToString(event_type), " }"});
  }

  std::string ExpectationToString() {
    return CallbackToString(expected_ifindex_, expected_ip_addr_,
                            expected_role_, expected_event_type_);
  }

  bool enabled_ = false;
  bool expectation_set_ = false;
  int expected_ifindex_ = -1;
  std::string expected_ip_addr_;
  NeighborLinkMonitor::NeighborRole expected_role_ =
      NeighborLinkMonitor::NeighborRole::kGateway;
  NeighborReachabilityEventSignal::EventType expected_event_type_ =
      NeighborReachabilityEventSignal::INVALID_EVENT_TYPE;
};

class NeighborLinkMonitorTest : public testing::Test {
 protected:
  void SetUp() override {
    mock_rtnl_handler_ = std::make_unique<net_base::MockRTNLHandler>();
    callback_ =
        base::BindRepeating(&FakeNeighborReachabilityEventHandler::Run,
                            base::Unretained(&fake_neighbor_event_handler_));
    link_monitor_ = std::make_unique<NeighborLinkMonitor>(
        kTestInterfaceIndex, kTestInterfaceName, mock_rtnl_handler_.get(),
        &callback_);
    ExpectAddRTNLListener();
  }

  void TearDown() override {
    // We should make sure |mock_rtnl_handler_| is valid during the life time of
    // |link_monitor_|.
    link_monitor_ = nullptr;
    mock_rtnl_handler_ = nullptr;
    registered_listener_ = nullptr;
  }

  void ExpectAddRTNLListener() {
    EXPECT_CALL(*mock_rtnl_handler_, AddListener(_))
        .WillRepeatedly(::testing::SaveArg<0>(&registered_listener_));
  }

  void NotifyNUDStateChanged(const std::string& addr, uint16_t nud_state) {
    CreateAndSendIncomingRTNLMessage(net_base::RTNLMessage::kModeAdd, addr,
                                     nud_state);
  }

  void NotifyNeighborRemoved(const std::string& addr) {
    CreateAndSendIncomingRTNLMessage(net_base::RTNLMessage::kModeDelete, addr,
                                     0);
  }

  void CreateAndSendIncomingRTNLMessage(const net_base::RTNLMessage::Mode mode,
                                        const std::string& address,
                                        uint16_t nud_state) {
    ASSERT_NE(registered_listener_, nullptr);

    const auto addr = net_base::IPAddress::CreateFromString(address);
    CHECK(addr);
    net_base::RTNLMessage msg(net_base::RTNLMessage::kTypeNeighbor, mode, 0, 0,
                              0, kTestInterfaceIndex,
                              net_base::ToSAFamily(addr->GetFamily()));
    msg.SetAttribute(NDA_DST, addr->ToBytes());
    if (mode == net_base::RTNLMessage::kModeAdd) {
      msg.set_neighbor_status(
          net_base::RTNLMessage::NeighborStatus(nud_state, 0, 0));
      msg.SetAttribute(NDA_LLADDR, std::vector<uint8_t>{1, 2, 3, 4, 5, 6});
    }

    registered_listener_->NotifyEvent(net_base::RTNLHandler::kRequestNeighbor,
                                      msg);
  }

  // The internal implementation of Timer uses Now() so we need
  // MOCK_TIME_AND_NOW here.
  base::test::TaskEnvironment task_env_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  FakeNeighborReachabilityEventHandler fake_neighbor_event_handler_;
  NeighborLinkMonitor::NeighborReachabilityEventHandler callback_;
  std::unique_ptr<net_base::MockRTNLHandler> mock_rtnl_handler_;
  std::unique_ptr<NeighborLinkMonitor> link_monitor_;
  net_base::RTNLListener* registered_listener_ = nullptr;
};

TEST_F(NeighborLinkMonitorTest, SendNeighborDumpMessageOnIPConfigChanged) {
  net_base::NetworkConfig ipconfig;
  ipconfig.ipv4_address =
      *net_base::IPv4CIDR::CreateFromCIDRString("1.2.3.4/24");
  ipconfig.ipv4_gateway = net_base::IPv4Address(1, 2, 3, 5);
  ipconfig.dns_servers = {*net_base::IPAddress::CreateFromString("1.2.3.6")};

  // On ipconfig changed, the link monitor should send only one dump request, to
  // fetch current NUD state of these new addresses.
  EXPECT_CALL(*mock_rtnl_handler_, DoSendMessage(IsNeighborDumpMessage(), _))
      .WillOnce(Return(true));

  link_monitor_->OnIPConfigChanged(ipconfig);
}

TEST_F(NeighborLinkMonitorTest, WatchLinkLocalIPv6DNSServerAddress) {
  net_base::NetworkConfig ipconfig;
  ipconfig.ipv6_addresses = {
      *net_base::IPv6CIDR::CreateFromCIDRString("2401::1/64")};
  ipconfig.ipv6_gateway = *net_base::IPv6Address::CreateFromString("fe80::1");
  ipconfig.dns_servers = {*net_base::IPAddress::CreateFromString("fe80::2")};

  link_monitor_->OnIPConfigChanged(ipconfig);

  EXPECT_CALL(*mock_rtnl_handler_,
              DoSendMessage(IsNeighborProbeMessage("fe80::1"), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_rtnl_handler_,
              DoSendMessage(IsNeighborProbeMessage("fe80::2"), _))
      .WillOnce(Return(true));

  NotifyNUDStateChanged("fe80::1", NUD_REACHABLE);
  NotifyNUDStateChanged("fe80::2", NUD_REACHABLE);
}

TEST_F(NeighborLinkMonitorTest, SendNeighborProbeMessage) {
  // Only the gateway should be in the watching list.
  net_base::NetworkConfig ipconfig;
  ipconfig.ipv4_address =
      *net_base::IPv4CIDR::CreateFromCIDRString("1.2.3.4/24");
  ipconfig.ipv4_gateway = net_base::IPv4Address(1, 2, 3, 5);
  link_monitor_->OnIPConfigChanged(ipconfig);

  // Creates a RTNL message about the NUD state of the gateway is NUD_REACHABLE
  // now. A probe message should be sent immediately after we know this address.
  EXPECT_CALL(*mock_rtnl_handler_,
              DoSendMessage(IsNeighborProbeMessage("1.2.3.5"), _))
      .WillOnce(Return(true));
  NotifyNUDStateChanged("1.2.3.5", NUD_REACHABLE);

  // Another probe message should be sent when the timer is triggered.
  EXPECT_CALL(*mock_rtnl_handler_,
              DoSendMessage(IsNeighborProbeMessage("1.2.3.5"), _))
      .WillOnce(Return(true));
  task_env_.FastForwardBy(NeighborLinkMonitor::kActiveProbeInterval);

  // If the state changed to NUD_PROBE, we should not probe this address again
  // when the timer is triggered.
  NotifyNUDStateChanged("1.2.3.5", NUD_PROBE);
  task_env_.FastForwardBy(NeighborLinkMonitor::kActiveProbeInterval);

  // The gateway is removed in the kernel. A dump request should be sent when
  // the timer is triggered.
  NotifyNeighborRemoved("1.2.3.5");
  EXPECT_CALL(*mock_rtnl_handler_, DoSendMessage(IsNeighborDumpMessage(), _))
      .WillOnce(Return(true));
  task_env_.FastForwardBy(NeighborLinkMonitor::kActiveProbeInterval);
}

TEST_F(NeighborLinkMonitorTest, UpdateWatchingEntries) {
  net_base::NetworkConfig ipconfig;
  ipconfig.ipv4_address =
      *net_base::IPv4CIDR::CreateFromCIDRString("1.2.3.4/24");
  ipconfig.ipv4_gateway = net_base::IPv4Address(1, 2, 3, 5);
  ipconfig.dns_servers = {*net_base::IPAddress::CreateFromString("1.2.3.6")};
  link_monitor_->OnIPConfigChanged(ipconfig);

  ipconfig.dns_servers = {*net_base::IPAddress::CreateFromString("1.2.3.7")};
  // One dump request is expected since there is a new address.
  EXPECT_CALL(*mock_rtnl_handler_, DoSendMessage(IsNeighborDumpMessage(), _))
      .WillOnce(Return(true));
  link_monitor_->OnIPConfigChanged(ipconfig);

  // Updates both addresses to NUD_PROBE (to avoid the link monitor sending a
  // probe request), and then NUD_REACHABLE state.
  NotifyNUDStateChanged("1.2.3.5", NUD_PROBE);
  NotifyNUDStateChanged("1.2.3.5", NUD_REACHABLE);
  NotifyNUDStateChanged("1.2.3.7", NUD_PROBE);
  NotifyNUDStateChanged("1.2.3.7", NUD_REACHABLE);

  // This address is not been watching now. Nothing should happen when a message
  // about it comes.
  NotifyNUDStateChanged("1.2.3.6", NUD_REACHABLE);

  // Nothing should happen within one interval.
  task_env_.FastForwardBy(NeighborLinkMonitor::kActiveProbeInterval / 2);

  // Checks if probe requests sent for both addresses when the timer is
  // triggered.
  EXPECT_CALL(*mock_rtnl_handler_,
              DoSendMessage(IsNeighborProbeMessage("1.2.3.5"), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_rtnl_handler_,
              DoSendMessage(IsNeighborProbeMessage("1.2.3.7"), _))
      .WillOnce(Return(true));
  task_env_.FastForwardBy(NeighborLinkMonitor::kActiveProbeInterval);
}

TEST_F(NeighborLinkMonitorTest, UpdateWatchingEntriesWithSameAddress) {
  net_base::NetworkConfig ipconfig;
  ipconfig.ipv4_address =
      *net_base::IPv4CIDR::CreateFromCIDRString("1.2.3.4/24");
  ipconfig.ipv4_gateway = net_base::IPv4Address(1, 2, 3, 5);
  ipconfig.dns_servers = {*net_base::IPAddress::CreateFromString("1.2.3.6")};
  link_monitor_->OnIPConfigChanged(ipconfig);

  // No dump request is expected.
  EXPECT_CALL(*mock_rtnl_handler_, DoSendMessage(IsNeighborDumpMessage(), _))
      .Times(0);
  link_monitor_->OnIPConfigChanged(ipconfig);
}

TEST_F(NeighborLinkMonitorTest, NotifyNeighborReachabilityEvent) {
  net_base::NetworkConfig ipconfig;
  ipconfig.ipv4_address =
      *net_base::IPv4CIDR::CreateFromCIDRString("1.2.3.4/24");
  ipconfig.ipv4_gateway = net_base::IPv4Address(1, 2, 3, 5);

  fake_neighbor_event_handler_.Enable();

  SCOPED_TRACE("Reachability is confirmed at the first time.");
  fake_neighbor_event_handler_.Expect(
      kTestInterfaceIndex, "1.2.3.5",
      NeighborLinkMonitor::NeighborRole::kGateway,
      NeighborReachabilityEventSignal::REACHABLE);
  link_monitor_->OnIPConfigChanged(ipconfig);
  NotifyNUDStateChanged("1.2.3.5", NUD_PROBE);
  NotifyNUDStateChanged("1.2.3.5", NUD_REACHABLE);
  NotifyNUDStateChanged("1.2.3.5", NUD_REACHABLE);
  NotifyNUDStateChanged("1.2.3.5", NUD_STALE);
  NotifyNUDStateChanged("1.2.3.5", NUD_PROBE);
  NotifyNUDStateChanged("1.2.3.5", NUD_REACHABLE);
  NotifyNUDStateChanged("1.2.3.5", NUD_STALE);
  NotifyNUDStateChanged("1.2.3.5", NUD_REACHABLE);

  SCOPED_TRACE("Messages with NUD_FAILED should trigger the callback once.");
  fake_neighbor_event_handler_.Expect(
      kTestInterfaceIndex, "1.2.3.5",
      NeighborLinkMonitor::NeighborRole::kGateway,
      NeighborReachabilityEventSignal::FAILED);
  NotifyNUDStateChanged("1.2.3.5", NUD_FAILED);
  NotifyNUDStateChanged("1.2.3.5", NUD_FAILED);
  NotifyNeighborRemoved("1.2.3.5");
}

TEST_F(NeighborLinkMonitorTest, NeighborRole) {
  net_base::NetworkConfig ipconfig;
  ipconfig.ipv4_address =
      *net_base::IPv4CIDR::CreateFromCIDRString("1.2.3.4/24");

  fake_neighbor_event_handler_.Enable();

  SCOPED_TRACE("On neighbor as gateway or DNS server failed.");
  ipconfig.ipv4_gateway = net_base::IPv4Address(1, 2, 3, 5);
  ipconfig.dns_servers = {*net_base::IPAddress::CreateFromString("1.2.3.6")};
  link_monitor_->OnIPConfigChanged(ipconfig);
  fake_neighbor_event_handler_.Expect(
      kTestInterfaceIndex, "1.2.3.5",
      NeighborLinkMonitor::NeighborRole::kGateway,
      NeighborReachabilityEventSignal::FAILED);
  NotifyNUDStateChanged("1.2.3.5", NUD_FAILED);
  fake_neighbor_event_handler_.Expect(
      kTestInterfaceIndex, "1.2.3.6",
      NeighborLinkMonitor::NeighborRole::kDNSServer,
      NeighborReachabilityEventSignal::FAILED);
  NotifyNUDStateChanged("1.2.3.6", NUD_FAILED);

  SCOPED_TRACE("Neighbors back to normal.");
  fake_neighbor_event_handler_.Disable();
  NotifyNUDStateChanged("1.2.3.5", NUD_REACHABLE);
  NotifyNUDStateChanged("1.2.3.6", NUD_REACHABLE);
  fake_neighbor_event_handler_.Enable();

  SCOPED_TRACE("On neighbor as gateway and DNS server failed");
  ipconfig.ipv4_gateway = net_base::IPv4Address(1, 2, 3, 5);
  ipconfig.dns_servers = {*net_base::IPAddress::CreateFromString("1.2.3.5")};
  link_monitor_->OnIPConfigChanged(ipconfig);
  fake_neighbor_event_handler_.Expect(
      kTestInterfaceIndex, "1.2.3.5",
      NeighborLinkMonitor::NeighborRole::kGatewayAndDNSServer,
      NeighborReachabilityEventSignal::FAILED);
  NotifyNUDStateChanged("1.2.3.5", NUD_FAILED);

  SCOPED_TRACE("Neighbors back to normal.");
  fake_neighbor_event_handler_.Disable();
  NotifyNUDStateChanged("1.2.3.5", NUD_REACHABLE);
  fake_neighbor_event_handler_.Enable();

  SCOPED_TRACE("Swaps the roles.");
  ipconfig.ipv4_gateway = net_base::IPv4Address(1, 2, 3, 6);
  ipconfig.dns_servers = {*net_base::IPAddress::CreateFromString("1.2.3.5")};
  link_monitor_->OnIPConfigChanged(ipconfig);
  fake_neighbor_event_handler_.Expect(
      kTestInterfaceIndex, "1.2.3.5",
      NeighborLinkMonitor::NeighborRole::kDNSServer,
      NeighborReachabilityEventSignal::FAILED);
  NotifyNUDStateChanged("1.2.3.5", NUD_FAILED);
  fake_neighbor_event_handler_.Expect(
      kTestInterfaceIndex, "1.2.3.6",
      NeighborLinkMonitor::NeighborRole::kGateway,
      NeighborReachabilityEventSignal::FAILED);
  NotifyNUDStateChanged("1.2.3.6", NUD_FAILED);
}

}  // namespace
}  // namespace patchpanel
