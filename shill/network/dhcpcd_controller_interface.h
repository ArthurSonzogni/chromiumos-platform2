// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_DHCPCD_CONTROLLER_INTERFACE_H_
#define SHILL_NETWORK_DHCPCD_CONTROLLER_INTERFACE_H_

#include <memory>
#include <string>
#include <string_view>

#include <base/functional/callback_forward.h>

#include "shill/store/key_value_store.h"
#include "shill/technology.h"

namespace shill {

// The interface of dhcpcd controller.
class DHCPCDControllerInterface {
 public:
  // The reason of the events sent by the dhcpcd process.
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

  // The handler of the events of the DHCPCDControllerInterface class. The
  // handler should be injected when creating the DHCPCDControllerInterface
  // instance.
  class EventHandler {
   public:
    // Called when the dhcpcd process sends the events. |configuration| contains
    // the extra information of the event.
    virtual void OnDHCPEvent(EventReason reason,
                             const KeyValueStore& configuration) = 0;

    // Called when the dhcpcd process is exited before the controller is
    // destroyed.
    virtual void OnProcessExited(int pid, int exit_status) = 0;
  };

  // Options to control the behavior of the DHCP client (dhcpcd).
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
    // The DHCP lease file will contain the suffix supplied in |lease_name| if
    // non-empty, otherwise the interface name will be used. This is for
    // differentiating the lease of one interface from another.
    std::string lease_name;
    // Hostname to be used in DHCP request. If it is not empty, it is placed in
    // the DHCP request to allow the server to map the request to a specific
    // user-named origin.
    std::string hostname;
  };

  DHCPCDControllerInterface(std::string_view interface, EventHandler* handler);
  virtual ~DHCPCDControllerInterface();

  // Returns true if the instance is ready to call other methods.
  virtual bool IsReady() const = 0;

  // Asks the dhcpcd process to rebind the interface.
  virtual bool Rebind() = 0;

  // Asks the dhcpcd process to release the lease on the interface.
  virtual bool Release() = 0;

  // Delegates the event of process exited to |handler_|.
  void OnProcessExited(int pid, int exit_status);

 protected:
  // The target network interface of the dhcpcd process.
  std::string interface_;

  // The event handler. It should outlives the controller instance.
  EventHandler* handler_;
};

// The interface of the DHCPCDControllerInterface's factory.
class DHCPCDControllerFactoryInterface {
 public:
  DHCPCDControllerFactoryInterface() = default;
  virtual ~DHCPCDControllerFactoryInterface() = default;

  // Creates a DHCPCDControllerInterface. Returns false if any error occurs.
  //
  // Note: the dhcpcd process might be ready asynchronously. Please use
  // DHCPCDControllerInterface::IsReady() to check if the instance is ready or
  // not.
  virtual std::unique_ptr<DHCPCDControllerInterface> Create(
      std::string_view interface,
      Technology technology,
      const DHCPCDControllerInterface::Options& options,
      DHCPCDControllerInterface::EventHandler* handler) = 0;
};

}  // namespace shill
#endif  // SHILL_NETWORK_DHCPCD_CONTROLLER_INTERFACE_H_
