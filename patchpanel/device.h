// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_DEVICE_H_
#define PATCHPANEL_DEVICE_H_

#include <linux/in6.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <map>
#include <memory>
#include <ostream>
#include <string>

#include <base/functional/bind.h>
#include <base/memory/weak_ptr.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "patchpanel/guest_type.h"
#include "patchpanel/ipc.h"
#include "patchpanel/mac_address_generator.h"
#include "patchpanel/subnet.h"

namespace patchpanel {

// Represents a virtual network interface created and managed by patchpanel with
// its configuration. A Device can be associated with:
//  - ARC container: a pair of virtual ethernet interfaces setup across the
//  host / ARC namespace boundary, plus a software bridge to which the host-side
//  veth interface is attached.
//  - ARCVM: a TAP device plus a software bridge to which the TAP device is
//  attached.
//  - Termina VMs, plugin VMs, other crosvm guests: a TAP device, with no
// software bridge.
// The main interface interacting with other parts of the network layer is:
//  - ARC, ARCVM: the software bridge.
//  - other crosvm guests: the TAP device.
// A Device always is always associated with a unique IPv4 subnet statically
// assigned by AddressManager based on the type of guest. Connected namespaces
// have currently no Device representation.
class Device {
 public:
  enum class ChangeEvent {
    kAdded,
    kRemoved,
  };
  using ChangeEventHandler = base::RepeatingCallback<void(
      const Device&, ChangeEvent, GuestMessage::GuestType)>;

  class Config {
   public:
    Config(const MacAddress& mac_addr,
           std::unique_ptr<Subnet> ipv4_subnet,
           std::unique_ptr<SubnetAddress> host_ipv4_addr,
           std::unique_ptr<SubnetAddress> guest_ipv4_addr,
           std::unique_ptr<Subnet> lxd_ipv4_subnet = nullptr);
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    ~Config() = default;

    MacAddress mac_addr() const { return mac_addr_; }
    void set_mac_addr(const MacAddress& mac) { mac_addr_ = mac; }

    uint32_t host_ipv4_addr() const { return host_ipv4_addr_->Address(); }
    uint32_t guest_ipv4_addr() const { return guest_ipv4_addr_->Address(); }

    const SubnetAddress* const host_ipv4_subnet_addr() const {
      return host_ipv4_addr_.get();
    }
    const SubnetAddress* const guest_ipv4_subnet_addr() const {
      return guest_ipv4_addr_.get();
    }

    const Subnet* const ipv4_subnet() const { return ipv4_subnet_.get(); }

    const Subnet* const lxd_ipv4_subnet() const {
      return lxd_ipv4_subnet_.get();
    }

    void set_tap_ifname(const std::string& tap);
    const std::string& tap_ifname() const;

   private:
    // A random MAC address assigned to the Device for the guest facing
    // interface.
    MacAddress mac_addr_;
    // The static IPv4 subnet allocated for this Device for the host and guest
    // facing interfaces.
    std::unique_ptr<Subnet> ipv4_subnet_;
    // The address allocated from |ipv4_subnet| for use by the host-side
    // interface associated with this Device. This is also used as the next hop
    // for the guest default route on the virtual network associated with that
    // Device.
    std::unique_ptr<SubnetAddress> host_ipv4_addr_;
    // The address allocated from |ipv4_subnet| for use by the guest-side
    // interface associated with this Device, if applicable.
    std::unique_ptr<SubnetAddress> guest_ipv4_addr_;
    // If applicable, an additional IPv4 subnet allocated for this Device for
    // guests like Crostini to use for assigning addresses to containers running
    // within the VM.
    std::unique_ptr<Subnet> lxd_ipv4_subnet_;
    // For VM guest, the interface name of the TAP device currently associated
    // with the configuration.
    std::string tap_;
  };

  // |type| the type of guest associated with this virtual device created by
  // patchpanel. |phys_ifname| corresponds either to the physical interface
  // provided by shill or a placeholder for a guest-specific control interface
  // (e.g. arc0). |host_ifname| identifies the name of the virtual (bridge)
  // interface. |guest_ifname|, if specified, identifies the name of the
  // interface used inside the guest.
  Device(GuestType type,
         const std::string& phys_ifname,
         const std::string& host_ifname,
         const std::string& guest_ifname,
         std::unique_ptr<Config> config);
  Device(const Device&) = delete;
  Device& operator=(const Device&) = delete;

  ~Device() = default;

  GuestType type() const { return type_; }
  const std::string& phys_ifname() const { return phys_ifname_; }
  const std::string& host_ifname() const { return host_ifname_; }
  const std::string& guest_ifname() const { return guest_ifname_; }
  Config& config() const;
  std::unique_ptr<Config> release_config();

 private:
  // The type of virtual device setup and guest.
  GuestType type_;
  // The physical interface managed by shill that this virtual device is
  // attached to. Only defined for ARC and ARCVM. Otherwise:
  //   - for ARC0, |phys_ifname_| is set to a fixed constant ("arc0"),
  //   - for virtual devices used by other crosvm guests, |phys_ifname_| is the
  //   same as |host_ifname_|.
  std::string phys_ifname_;
  // The name of the main virtual interface created by patchpanel for carrying
  // packets out of the guest environment and onto the host routing setup. For
  // all ARC virtual devices, |host_ifname_| corresponds to the virtual bridge
  // created with Datapath::AddBridge(). For other crosvm guests (Termina,
  // Crostini, PluginVM, etc) this corresponds to the TAP device created with
  // Datapath::AddTAP().
  std::string host_ifname_;
  // The name of the virtual interface used inside the guest environment. Only
  // available for ARC virtual devices, otherwise empty for other crosvm guests.
  std::string guest_ifname_;
  // The MAC address and IPv4 configuration for this virtual device.
  std::unique_ptr<Config> config_;

  FRIEND_TEST(DeviceTest, DisableLegacyAndroidDeviceSendsTwoMessages);

  base::WeakPtrFactory<Device> weak_factory_{this};
};

std::ostream& operator<<(std::ostream& stream, const Device& device);

}  // namespace patchpanel

#endif  // PATCHPANEL_DEVICE_H_
