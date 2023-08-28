// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/qos_service.h"

#include <memory>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "net-base/ipv4_address.h"
#include "patchpanel/mock_datapath.h"
#include "patchpanel/routing_service.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {
namespace {

using ::testing::_;
using ::testing::ElementsAreArray;
using ::testing::Mock;
using ::testing::StrEq;
using ::testing::StrictMock;

constexpr char kIPAddress1[] = "8.8.8.8";
constexpr char kIPAddress2[] = "8.8.8.4";
constexpr int kPort1 = 10000;
constexpr int kPort2 = 20000;

class MockProcessRunner : public MinijailedProcessRunner {
 public:
  MockProcessRunner() = default;
  ~MockProcessRunner() = default;

  MOCK_METHOD(int,
              conntrack,
              (std::string_view command,
               const std::vector<std::string>& argv,
               bool log_failures),
              (override));
};

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
  QoSService qos_svc(&datapath);

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
  QoSService qos_svc(&datapath, std::move(runner));
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

}  // namespace
}  // namespace patchpanel
