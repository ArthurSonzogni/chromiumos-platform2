// Copyright 2018 The Chromium OS Authors. All rights reserved.
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
#include <string>

#include <base/bind.h>
#include <base/memory/weak_ptr.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "patchpanel/guest_type.h"
#include "patchpanel/ipc.pb.h"
#include "patchpanel/mac_address_generator.h"
#include "patchpanel/subnet.h"

namespace patchpanel {

// Encapsulates a physical (e.g. eth0) or proxy (e.g. arc) network device and
// its configuration spec (interfaces, addresses) on the host and in the
// container. It manages additional services such as router detection, address
// assignment, and MDNS and SSDP forwarding. This class is the authoritative
// source for configuration events.
class Device {
 public:
  enum class ChangeEvent {
    ADDED,
    REMOVED,
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
    // A random MAC address assigned to the device.
    MacAddress mac_addr_;
    // The IPV4 subnet allocated for this device.
    std::unique_ptr<Subnet> ipv4_subnet_;
    // The address allocated from |ipv4_subnet| for use by the CrOS-side
    // interface associated with this device.
    std::unique_ptr<SubnetAddress> host_ipv4_addr_;
    // The address allocated from |ipv4_subnet| for use by the guest-side
    // interface associated with this device, if applicable.
    std::unique_ptr<SubnetAddress> guest_ipv4_addr_;
    // If applicable, an additional subnet allocated for this device for guests
    // like Crostini to use for assigning addresses to containers running within
    // the VM.
    std::unique_ptr<Subnet> lxd_ipv4_subnet_;
    // TAP devices currently associated with the configuration.
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
  GuestType type_;
  std::string phys_ifname_;
  std::string host_ifname_;
  std::string guest_ifname_;
  std::unique_ptr<Config> config_;

  FRIEND_TEST(DeviceTest, DisableLegacyAndroidDeviceSendsTwoMessages);

  base::WeakPtrFactory<Device> weak_factory_{this};
};

std::ostream& operator<<(std::ostream& stream, const Device& device);

}  // namespace patchpanel

#endif  // PATCHPANEL_DEVICE_H_
