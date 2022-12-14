// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_SLAAC_CONTROLLER_H_
#define SHILL_NETWORK_SLAAC_CONTROLLER_H_

#include <memory>
#include <vector>

#include <base/memory/weak_ptr.h>

#include "shill/net/ip_address.h"
#include "shill/net/rtnl_handler.h"
#include "shill/net/rtnl_listener.h"
#include "shill/net/shill_time.h"

namespace shill {

class Network;

class SLAACController {
 public:
  SLAACController(int interface_index,
                  Network* network,
                  RTNLHandler* rtnl_handler);
  virtual ~SLAACController();

  void StartRTNL();

  // Get the IPv6 DNS server addresses received from RDNSS. This method
  // returns true and sets |address_list| and |life_time_seconds| if the IPv6
  // DNS server addresses exists. Otherwise, it returns false and leave
  // |address_list| and |life_time_seconds| unmodified. |life_time_seconds|
  // indicates the number of the seconds the DNS server is still valid for at
  // the time of this function call. Value of 0 means the DNS server is not
  // valid anymore, and value of 0xFFFFFFFF means the DNS server is valid
  // forever.
  virtual bool GetIPv6DNSServerAddresses(std::vector<IPAddress>* address_list,
                                         uint32_t* life_time_seconds);

 private:
  // TODO(b/227563210): Refactor to remove friend declaration after moving all
  // SLAAC functionality from DeviceInfo and Network to SLAACController.
  friend class SLAACControllerTest;

  void AddressMsgHandler(const RTNLMessage& msg);
  void RDNSSMsgHandler(const RTNLMessage& msg);

  const int interface_index_;
  std::vector<IPAddress> ipv6_dns_server_addresses_;
  uint32_t ipv6_dns_server_lifetime_seconds_;
  time_t ipv6_dns_server_received_time_seconds_;

  // Pointer back to Network is only temporary during SLAAC refactor. Will be
  // removed (and replaced by event callbacks) when refactor is finished.
  Network* network_;

  Time* time_;
  RTNLHandler* rtnl_handler_;
  std::unique_ptr<RTNLListener> address_listener_;
  std::unique_ptr<RTNLListener> rdnss_listener_;

  base::WeakPtrFactory<SLAACController> weak_factory_{this};
};

}  // namespace shill

#endif  // SHILL_NETWORK_SLAAC_CONTROLLER_H_
