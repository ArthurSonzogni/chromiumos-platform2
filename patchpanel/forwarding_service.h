// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_FORWARDING_SERVICE_H_
#define PATCHPANEL_FORWARDING_SERVICE_H_

#include <optional>
#include <string>

#include <net-base/ip_address.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/multicast_forwarder.h"
#include "patchpanel/network_monitor_service.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {

class ForwardingService {
 public:
  // Starts IPv6 ND proxy forwarding between the upstream |shill_device| and
  // the downstream interface or guest |ifname_virtual|.
  virtual void StartIPv6NDPForwarding(
      const ShillClient::Device& shill_device,
      std::string_view ifname_virtual,
      std::optional<int> mtu = std::nullopt,
      std::optional<int> hop_limit = std::nullopt) = 0;

  // Stops IPv6 ND proxy forwarding between the upstream |shill_device| and
  // the downstream interface or guest |ifname_virtual|.
  virtual void StopIPv6NDPForwarding(const ShillClient::Device& shill_device,
                                     std::string_view ifname_virtual) = 0;

  // Starts broadcast forwarding between the upstream |shill_device| and
  // the downstream interface or guest |ifname_virtual|.
  virtual void StartBroadcastForwarding(const ShillClient::Device& shill_device,
                                        std::string_view ifname_virtual) = 0;

  // Stops broadcast forwarding between the upstream |shill_device| and
  // the downstream interface or guest |ifname_virtual|.
  virtual void StopBroadcastForwarding(const ShillClient::Device& shill_device,
                                       std::string_view ifname_virtual) = 0;

  // Starts multicast forwarding between the upstream |shill_device| and
  // the downstream interface or guest |ifname_virtual|. |dir| specifies the
  // direction of forwarding to be started.
  virtual void StartMulticastForwarding(
      const ShillClient::Device& shill_device,
      std::string_view ifname_virtual,
      MulticastForwarder::Direction dir =
          MulticastForwarder::Direction::kTwoWays) = 0;

  // Stops multicast forwarding between the upstream |shill_device| and
  // the downstream interface or guest |ifname_virtual|. |dir| specifies the
  // direction of forwarding to be stopped.
  virtual void StopMulticastForwarding(
      const ShillClient::Device& shill_device,
      std::string_view ifname_virtual,
      MulticastForwarder::Direction dir =
          MulticastForwarder::Direction::kTwoWays) = 0;
};

}  // namespace patchpanel
#endif  // PATCHPANEL_FORWARDING_SERVICE_H_
