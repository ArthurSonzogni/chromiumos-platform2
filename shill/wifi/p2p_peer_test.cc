// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/p2p_peer.h"

#include <memory>
#include <string>
#include <utility>

#include <base/test/mock_callback.h>
#include <chromeos/net-base/mac_address.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/mock_control.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/supplicant/mock_supplicant_peer_proxy.h"
#include "shill/supplicant/wpa_supplicant.h"
#include "shill/test_event_dispatcher.h"
#include "shill/wifi/local_device.h"
#include "shill/wifi/mock_p2p_device.h"

using testing::_;
using testing::ByMove;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace shill {

namespace {
const char kPrimaryInterfaceName[] = "wlan0";
const char kInterfaceName[] = "p2p-wlan0-0";
const uint32_t kPhyIndex = 5678;
const uint32_t kShillId = 0;
const uint32_t kPeerId = 0;
WiFiPhy::Priority kPriority = WiFiPhy::Priority(0);
}  // namespace

class P2PPeerTest : public testing::Test {
 public:
  P2PPeerTest()
      : manager_(&control_interface_, &dispatcher_, &metrics_),
        p2p_device_(new NiceMock<MockP2PDevice>(&manager_,
                                                LocalDevice::IfaceType::kP2PGO,
                                                kPrimaryInterfaceName,
                                                kPhyIndex,
                                                kShillId,
                                                kPriority,
                                                event_cb_.Get())),
        peer_proxy_(new NiceMock<MockSupplicantPeerProxy>()) {
    std::unique_ptr<MockSupplicantPeerProxy> peer_proxy(peer_proxy_);
    ON_CALL(control_interface_, CreateSupplicantPeerProxy(_))
        .WillByDefault(Return(ByMove(std::move(peer_proxy))));
  }

  dbus::ObjectPath DefaultPeerObjectPath(int peer_id) {
    return dbus::ObjectPath(
        std::string("/interface/") + std::string(kInterfaceName) +
        std::string("/Peers/deadbeef01") + std::to_string(peer_id));
  }

  ByteArray DefaultPeerAddress(int peer_id) {
    std::string mac_address =
        std::string("de:ad:be:ef:01:0") + std::to_string(peer_id);
    return net_base::MacAddress::CreateFromString(mac_address)->ToBytes();
  }

  KeyValueStore DefaultPeerProperties(int peer_id) {
    KeyValueStore properties;
    properties.Set<ByteArray>(WPASupplicant::kPeerPropertyDeviceAddress,
                              DefaultPeerAddress(peer_id));
    return properties;
  }

 protected:
  StrictMock<base::MockRepeatingCallback<void(LocalDevice::DeviceEvent,
                                              const LocalDevice*)>>
      event_cb_;
  NiceMock<MockControl> control_interface_;
  EventDispatcherForTest dispatcher_;
  NiceMock<MockMetrics> metrics_;
  NiceMock<MockManager> manager_;
  MockP2PDevice* p2p_device_;
  MockSupplicantPeerProxy* peer_proxy_;
};

TEST_F(P2PPeerTest, GetPeerProperties) {
  EXPECT_CALL(*peer_proxy_, GetProperties(_))
      .WillOnce(DoAll(SetArgPointee<0>(DefaultPeerProperties(kPeerId)),
                      Return(true)));
  std::unique_ptr<P2PPeer> p2p_peer = std::make_unique<P2PPeer>(
      p2p_device_, DefaultPeerObjectPath(kPeerId), &control_interface_);

  auto peer_properties = p2p_peer->GetPeerProperties();
  EXPECT_TRUE(
      base::Contains(peer_properties, kP2PGroupInfoClientMACAddressProperty));
  EXPECT_EQ(peer_properties[kP2PGroupInfoClientMACAddressProperty],
            net_base::MacAddress::CreateFromBytes(DefaultPeerAddress(kPeerId))
                ->ToString());
}

}  // namespace shill
