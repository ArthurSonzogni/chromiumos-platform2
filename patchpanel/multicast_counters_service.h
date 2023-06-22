// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_MULTICAST_COUNTERS_SERVICE_H_
#define PATCHPANEL_MULTICAST_COUNTERS_SERVICE_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/containers/fixed_flat_map.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/datapath.h"
#include "patchpanel/iptables.h"
#include "patchpanel/routing_service.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {

// This class manages the iptables rules for multicast packet counters, and
// queries iptables to get the counters when a request comes. This class will
// set up several iptable rules to track the counters for each possible
// combination of (mDNS, SSDP) x (ethernet, WiFi) x (shill device) x {IPv4,
// IPv6}. These counters will be set up when s/service/patchpanel/ starts and
// deleted when s/service/patchpanel/ stops.
// These counters only count ingress traffic for the reason that ingress is the
// dominant direction for multicast packets and receiving inbound traffic and
// processing the packets is the main source of power consumption.
//
// Implementation details:
//
// For iptables rules, we add rx_(ethernet|wifi)_(mdns|ssdp) and rx_(mdns|ssdp)
// chains to the mangle table when s/service/patchpanel/ starts, and add/delete
// jumping rules for interfaces individually when devices are added or removed.
// When queried, two commands (iptables and ip6tables) will be executed to get
// mangle tables output and to get the counters, and the packet number will be
// total number both IP families.
class MulticastCountersService {
 public:
  enum class MulticastProtocolType {
    kMdns = 0,
    kSsdp = 1,
  };

  // Only ethernet and WiFi are considered here as we don’t expect
  // multicast on cell or VPNs.
  enum class MulticastTechnologyType {
    kEthernet = 0,
    kWifi = 1,
  };

  using CounterKey = std::pair<MulticastProtocolType, MulticastTechnologyType>;

  explicit MulticastCountersService(Datapath* datapath);
  virtual ~MulticastCountersService() = default;

  // Adds initial iptables chains and counter rules for both IPv6 and IPv4 for
  // mDNS and SSDP.
  virtual void Start();
  // Deletes iptables chains and counter rules added in Start().
  virtual void Stop();
  // Adds jump rules for a new physical device if this is the first time this
  // device is seen.
  virtual void OnPhysicalDeviceAdded(const ShillClient::Device& device);
  // Collects and returns packet counters from all the existing iptables rules
  // for multicast, divided by technology (ethernet, wifi) and protocol (ssdp,
  // mdns) in CounterKey, and recorded by packet number.
  virtual std::optional<
      std::map<MulticastCountersService::CounterKey, uint64_t>>
  GetCounters();

 private:
  // Installs jump rules for an interface to count ingress multicast traffic
  // of |ifname|.
  virtual void SetupJumpRules(Iptables::Command command,
                              base::StringPiece ifname,
                              base::StringPiece technology);
  // Parses the output of `iptables -L -x -v` (or `ip6tables`) and adds the
  // parsed values into the corresponding counters in |counters|.
  // This function will try to find the pattern of:
  //   <one chain line for an accounting chain>
  //   <one header line>
  //   <one counter line for an accounting rule>
  // The protocol name will be extracted from the chain line, and then the
  // values extracted from the counter line will be added into the counter
  // for that interface. Note that this function will not fully validate
  // if |output| is an output from iptables.
  virtual bool ParseIptableOutput(base::StringPiece output,
                                  std::map<CounterKey, uint64_t>* counter);

  Datapath* datapath_;
  std::vector<std::pair<std::string, std::string>> interfaces_;
};

}  // namespace patchpanel

#endif  // PATCHPANEL_MULTICAST_COUNTERS_SERVICE_H_
