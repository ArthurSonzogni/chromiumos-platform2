// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_FORWARDING_SERVICE_H_
#define PATCHPANEL_FORWARDING_SERVICE_H_

#include <optional>
#include <string>

#include <net-base/ip_address.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/network_monitor_service.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {

class ForwardingService {
 public:
  // Struct to specify which forwarders to start and stop.
  struct ForwardingSet {
    bool ipv6;
    bool multicast;

    bool operator==(const ForwardingSet& b) const {
      return ipv6 == b.ipv6 && multicast == b.multicast;
    }
  };

  // Starts IPv6 and multicast forwarding as specified in |fs| between the
  // upstream |shill_device| and the dowsntream interface or guest
  // |ifname_virtual|.
  virtual void StartForwarding(const ShillClient::Device& shill_device,
                               const std::string& ifname_virtual,
                               const ForwardingSet& fs = {.ipv6 = true,
                                                          .multicast = true},
                               std::optional<int> mtu = std::nullopt,
                               std::optional<int> hop_limit = std::nullopt) = 0;

  // Stops IPv6 and multicast forwarding as specified in |fs| between the
  // upstream |shill_device| and the dowsntream interface or guest
  // |ifname_virtual|.
  virtual void StopForwarding(const ShillClient::Device& shill_device,
                              const std::string& ifname_virtual,
                              const ForwardingSet& fs = {
                                  .ipv6 = true, .multicast = true}) = 0;
};

}  // namespace patchpanel
#endif  // PATCHPANEL_FORWARDING_SERVICE_H_
