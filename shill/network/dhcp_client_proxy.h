// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_DHCP_CLIENT_PROXY_H_
#define SHILL_NETWORK_DHCP_CLIENT_PROXY_H_

#include <memory>
#include <string>
#include <string_view>

#include <base/functional/callback_forward.h>
#include <chromeos/net-base/network_config.h>

#include "shill/network/dhcpv4_config.h"
#include "shill/technology.h"

namespace shill {

// The interface of the DHCP client's proxy.
class DHCPClientProxy {
 public:
  // The reason of the events sent by the DHCP client process.
  enum class EventReason {
    kBound,
    kFail,
    kGatewayArp,
    kNak,
    kRebind,
    kReboot,
    kRenew,
    kIPv6OnlyPreferred,
  };

  // Returns true if the EventHandler::OnDHCPEvent() with |reason| needs the
  // extra information.
  static bool NeedConfiguration(DHCPClientProxy::EventReason reason);

  // The handler of the events of the DHCPClientProxy class. The handler should
  // be injected when creating the DHCPClientProxy instance.
  class EventHandler {
   public:
    // Called when the DHCP client sends the events. |network_config| and
    // |dhcp_data| contain the extra information of the event.
    virtual void OnDHCPEvent(EventReason reason,
                             const net_base::NetworkConfig& network_config,
                             const DHCPv4Config::Data& dhcp_data) = 0;

    // Called when the DHCP client process is exited before the proxy is
    // destroyed.
    virtual void OnProcessExited(int pid, int exit_status) = 0;
  };

  // Options to control the behavior of the DHCP client.
  struct Options {
    // If true, the DHCP client will ARP for the gateway IP address as an
    // additional safeguard against the issued IP address being in-use by
    // another station.
    bool use_arp_gateway = false;
    // If true, the DHCP client will request option 108 to prefer IPv6-only on a
    // capable network.
    bool use_rfc_8925 = false;
    // If true, the DHCP client will set the DSCP field of the egress packets to
    // 48 (the Network Control category) for better QoS. Currently, this option
    // is limited to the WiFi networks.
    bool apply_dscp = false;
    // Hostname to be used in DHCP request. If it is not empty, it is placed in
    // the DHCP request to allow the server to map the request to a specific
    // user-named origin.
    std::string hostname;

    friend bool operator==(const Options&, const Options&);
  };

  DHCPClientProxy(std::string_view interface, EventHandler* handler);
  virtual ~DHCPClientProxy();

  // Returns true if the instance is ready to call other methods.
  virtual bool IsReady() const = 0;

  // Asks the DHCP client to rebind the interface.
  virtual bool Rebind() = 0;

  // Asks the DHCP client to release the lease on the interface.
  virtual bool Release() = 0;

  // Delegates the event of process exited to |handler_|.
  void OnProcessExited(int pid, int exit_status);

 protected:
  // The target network interface of the DHCP client.
  std::string interface_;

  // The event handler. It should outlives the proxy instance.
  EventHandler* handler_;
};

// The interface of the DHCPClientProxy's factory.
class DHCPClientProxyFactory {
 public:
  DHCPClientProxyFactory() = default;
  virtual ~DHCPClientProxyFactory() = default;

  // Creates a DHCPClientProxy. Returns nullptr if any error occurs.
  //
  // Note: the DHCP client process might be ready asynchronously. Please use
  // DHCPClientProxy::IsReady() to check if the instance is ready or not.
  virtual std::unique_ptr<DHCPClientProxy> Create(
      std::string_view interface,
      Technology technology,
      const DHCPClientProxy::Options& options,
      DHCPClientProxy::EventHandler* handler) = 0;
};

}  // namespace shill
#endif  // SHILL_NETWORK_DHCP_CLIENT_PROXY_H_
