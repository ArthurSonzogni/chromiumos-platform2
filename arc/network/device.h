// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_NETWORK_DEVICE_H_
#define ARC_NETWORK_DEVICE_H_

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

#include "arc/network/ipc.pb.h"
#include "arc/network/mac_address_generator.h"
#include "arc/network/neighbor_finder.h"
#include "arc/network/router_finder.h"
#include "arc/network/subnet.h"

namespace arc_networkd {

// Reserved name for the Android device.
extern const char kAndroidDevice[];
// Reserved name for the Android device for legacy single network configs.
extern const char kAndroidLegacyDevice[];
// Reserved name for the Android device for ARCVM.
// TODO(garrick): This can be removed
extern const char kAndroidVmDevice[];
// Reserved name prefix for Termina VM devices;
extern const char kTerminaVmDevicePrefix[];

// Encapsulates a physical (e.g. eth0) or proxy (e.g. arc) network device and
// its configuration spec (interfaces, addresses) on the host and in the
// container. It manages additional services such as router detection, address
// assignment, and MDNS and SSDP forwarding. This class is the authoritative
// source for configuration events.
class Device {
 public:
  using DeviceHandler = base::Callback<void(Device*)>;

  class Context {
   public:
    virtual ~Context() = default;
    virtual bool IsLinkUp() const = 0;

   protected:
    Context() = default;

   private:
    DISALLOW_COPY_AND_ASSIGN(Context);
  };

  class Config {
   public:
    Config(const std::string& host_ifname,
           const std::string& guest_ifname,
           const MacAddress& guest_mac_addr,
           std::unique_ptr<Subnet> ipv4_subnet,
           std::unique_ptr<SubnetAddress> host_ipv4_addr,
           std::unique_ptr<SubnetAddress> guest_ipv4_addr,
           std::unique_ptr<Subnet> lxd_ipv4_subnet = nullptr);
    ~Config() = default;

    std::string host_ifname() const { return host_ifname_; }
    std::string guest_ifname() const { return guest_ifname_; }
    MacAddress guest_mac_addr() const { return guest_mac_addr_; }
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

    friend std::ostream& operator<<(std::ostream& stream, const Device& device);

   private:
    // The name of the interface created on the CrOS side. This should always
    // be defined.
    std::string host_ifname_;
    // If applicable, the name of the device interface exposed in the guest. For
    // example, for ARC P, this name will match the physical device name.
    std::string guest_ifname_;
    // A random MAC address assigned to the device.
    MacAddress guest_mac_addr_;
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

    DISALLOW_COPY_AND_ASSIGN(Config);
  };

  struct Options {
    bool fwd_multicast;
    bool ipv6_enabled;
    bool find_ipv6_routes_legacy;

    // Indicates this device must track shill's default interface.
    // TODO(garrick): Further qualify if this interface is a physical interface
    // or an ARC VPN to match the distinction shill is making; specifically, ARC
    // N should not loop back into itself but for Termina this should flow over
    // the VPN.
    bool use_default_interface;

    // Indicates this is a special device used for Android. In single-networked
    // guests (like ARC N) it is the only bridge into the container; in
    // multi-networked guests it is used (only) to support VPNs and ADB over
    // TCP.
    bool is_android;

    // Indicates this device is managed directly by a guest service and should
    // not be mutated in response to shill updates.
    bool is_sticky;
  };

  struct IPv6Config {
    IPv6Config() : prefix_len(0), addr_attempts(0) {}

    void clear();

    struct in6_addr addr;
    struct in6_addr router;
    int prefix_len;
    std::string ifname;
    int addr_attempts;
  };

  Device(const std::string& ifname,
         std::unique_ptr<Config> config,
         const Options& options,
         GuestMessage::GuestType guest);
  ~Device() = default;

  const std::string& ifname() const;
  Config& config() const;
  IPv6Config& ipv6_config();
  const Options& options() const;

  void set_context(std::unique_ptr<Context> ctx);
  Context* context();

  bool IsAndroid() const;
  bool IsArc() const;
  bool IsCrostini() const;

  // Host interface name for Termina devices is only determined on creation, so
  // this function needs to be used rather than directly reading from config().
  // This is a hack and should be cleaned up by b/145483351.
  std::string HostInterfaceName() const;
  bool UsesDefaultInterface() const;

  void RegisterIPv6SetupHandler(const DeviceHandler& handler);
  void RegisterIPv6TeardownHandler(const DeviceHandler& handler);

  // Returns true if the link status changed.
  bool HostLinkUp(bool link_up);

  bool IsFullyUp() const;

  void Enable(const std::string& ifname);
  void Disable();

  void StartIPv6RoutingLegacy(const std::string& ifname);
  void StopIPv6RoutingLegacy();

  void OnGuestStart(GuestMessage::GuestType guest);
  void OnGuestStop(GuestMessage::GuestType guest);

  friend std::ostream& operator<<(std::ostream& stream, const Device& device);

 private:
  // Callback from RouterFinder.  May be triggered multiple times, e.g.
  // if the route disappears or changes.
  void OnRouteFound(const struct in6_addr& prefix,
                    int prefix_len,
                    const struct in6_addr& router);

  // Callback from NeighborFinder to indicate whether an IPv6 address
  // collision was found or not found.
  void OnNeighborCheckResult(bool found);

  const std::string ifname_;
  std::unique_ptr<Config> config_;
  const Options options_;
  GuestMessage::GuestType guest_;
  std::unique_ptr<Context> ctx_;

  // Indicates if the host-side interface is up. Guest-size interfaces
  // may be tracked in the guest-specific context.
  bool host_link_up_;

  IPv6Config ipv6_config_;
  DeviceHandler ipv6_up_handler_;
  DeviceHandler ipv6_down_handler_;

  std::unique_ptr<RouterFinder> router_finder_;
  std::unique_ptr<NeighborFinder> neighbor_finder_;

  base::WeakPtrFactory<Device> weak_factory_{this};

  FRIEND_TEST(DeviceTest, DisableLegacyAndroidDeviceSendsTwoMessages);
  DISALLOW_COPY_AND_ASSIGN(Device);
};

}  // namespace arc_networkd

#endif  // ARC_NETWORK_DEVICE_H_
