// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/network/device.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <map>
#include <utility>

#include <base/bind.h>
#include <base/lazy_instance.h>
#include <base/logging.h>

#include "arc/network/arc_ip_config.h"
#include "arc/network/net_util.h"

namespace arc_networkd {

// These are used to identify which ARC++ data path should be used when setting
// up the Android device.
const char kAndroidDevice[] = "arc0";
const char kAndroidLegacyDevice[] = "android";

namespace {
constexpr uint32_t kMdnsMcastAddress = Ipv4Addr(224, 0, 0, 251);
constexpr uint16_t kMdnsPort = 5353;
constexpr uint32_t kSsdpMcastAddress = Ipv4Addr(239, 255, 255, 250);
constexpr uint16_t kSsdpPort = 1900;
constexpr int kMaxRandomAddressTries = 3;
}  // namespace

Device::Config::Config(const std::string& host_ifname,
                       const std::string& guest_ifname,
                       const MacAddress& guest_mac_addr,
                       std::unique_ptr<Subnet> ipv4_subnet,
                       std::unique_ptr<SubnetAddress> host_ipv4_addr,
                       std::unique_ptr<SubnetAddress> guest_ipv4_addr)
    : host_ifname_(host_ifname),
      guest_ifname_(guest_ifname),
      guest_mac_addr_(guest_mac_addr),
      ipv4_subnet_(std::move(ipv4_subnet)),
      host_ipv4_addr_(std::move(host_ipv4_addr)),
      guest_ipv4_addr_(std::move(guest_ipv4_addr)) {}

Device::Device(const std::string& ifname,
               std::unique_ptr<Device::Config> config,
               const Device::Options& options,
               const MessageSink& msg_sink)
    : ifname_(ifname),
      config_(std::move(config)),
      options_(options),
      msg_sink_(msg_sink),
      host_link_up_(false),
      guest_link_up_(false) {
  DCHECK(config_);
  if (msg_sink_.is_null())
    return;

  DeviceMessage msg;
  msg.set_dev_ifname(ifname_);
  auto* dev_config = msg.mutable_dev_config();
  FillProto(dev_config);
  msg_sink_.Run(msg);
}

Device::~Device() {
  if (msg_sink_.is_null())
    return;

  DeviceMessage msg;
  msg.set_dev_ifname(ifname_);
  msg.set_teardown(true);
  msg_sink_.Run(msg);
}

void Device::FillProto(DeviceConfig* msg) const {
  msg->set_br_ifname(config_->host_ifname());
  msg->set_br_ipv4(IPv4AddressToString(config_->host_ipv4_addr()));
  msg->set_arc_ifname(config_->guest_ifname());
  msg->set_arc_ipv4(IPv4AddressToString(config_->guest_ipv4_addr()));
  msg->set_mac_addr(MacAddressToString(config_->guest_mac_addr()));

  msg->set_fwd_multicast(options_.fwd_multicast);
  msg->set_find_ipv6_routes(options_.find_ipv6_routes);
}

const std::string& Device::ifname() const {
  return ifname_;
}

Device::Config& Device::config() const {
  CHECK(config_);
  return *config_.get();
}

const Device::Options& Device::options() const {
  return options_;
}

bool Device::IsAndroid() const {
  return ifname_ == kAndroidDevice;
}

bool Device::IsLegacyAndroid() const {
  return ifname_ == kAndroidLegacyDevice;
}

bool Device::LinkUp(const std::string& ifname, bool up) {
  bool* link_up =
      (ifname == config_->host_ifname())
          ? &host_link_up_
          : (ifname == config_->guest_ifname()) ? &guest_link_up_ : nullptr;
  if (!link_up) {
    LOG(DFATAL) << "Unknown interface: " << ifname;
    return false;
  }

  if (up == *link_up)
    return false;

  if (!up)
    Disable();

  *link_up = up;
  return true;
}

void Device::Enable(const std::string& ifname) {
  if (!host_link_up_ || !guest_link_up_)
    return;

  if (options_.fwd_multicast) {
    if (!mdns_forwarder_) {
      LOG(INFO) << "Enabling mDNS forwarding for device " << ifname_;
      auto mdns_fwd = std::make_unique<MulticastForwarder>();
      if (mdns_fwd->Start(config_->host_ifname(), ifname,
                          config_->guest_ipv4_addr(), kMdnsMcastAddress,
                          kMdnsPort,
                          /* allow_stateless */ true)) {
        mdns_forwarder_ = std::move(mdns_fwd);
      } else {
        LOG(WARNING) << "mDNS forwarder could not be started on " << ifname_;
      }
    }

    if (!ssdp_forwarder_) {
      LOG(INFO) << "Enabling SSDP forwarding for device " << ifname_;
      auto ssdp_fwd = std::make_unique<MulticastForwarder>();
      if (ssdp_fwd->Start(config_->host_ifname(), ifname, htonl(INADDR_ANY),
                          kSsdpMcastAddress, kSsdpPort,
                          /* allow_stateless */ false)) {
        ssdp_forwarder_ = std::move(ssdp_fwd);
      } else {
        LOG(WARNING) << "SSDP forwarder could not be started on " << ifname_;
      }
    }
  }

  if (options_.find_ipv6_routes && !router_finder_) {
    LOG(INFO) << "Enabling IPV6 route finding for device " << ifname_
              << " on interface " << ifname;
    legacy_lan_ifname_ = ifname;
    router_finder_.reset(new RouterFinder());
    router_finder_->Start(
        ifname, base::Bind(&Device::OnRouteFound, weak_factory_.GetWeakPtr()));
  }
}

void Device::Disable() {
  LOG(INFO) << "Disabling device " << ifname_;

  neighbor_finder_.reset();
  router_finder_.reset();
  ssdp_forwarder_.reset();
  mdns_forwarder_.reset();

  if (msg_sink_.is_null())
    return;

  // Clear IPv6 info, if necessary.
  if (options_.find_ipv6_routes) {
    DeviceMessage msg;
    msg.set_dev_ifname(ifname_);
    msg.set_clear_arc_ip(true);
    msg_sink_.Run(msg);
  }
}

void Device::OnGuestStart(GuestMessage::GuestType guest) {
  host_link_up_ = false;
  guest_link_up_ = false;
}

void Device::OnGuestStop(GuestMessage::GuestType guest) {}

void Device::OnRouteFound(const struct in6_addr& prefix,
                          int prefix_len,
                          const struct in6_addr& router) {
  const std::string& ifname =
      legacy_lan_ifname_.empty() ? ifname_ : legacy_lan_ifname_;

  if (prefix_len == 64) {
    LOG(INFO) << "Found IPv6 network on iface " << ifname << " route=" << prefix
              << "/" << prefix_len << ", gateway=" << router;

    memcpy(&random_address_, &prefix, sizeof(random_address_));
    random_address_prefix_len_ = prefix_len;
    random_address_tries_ = 0;

    ArcIpConfig::GenerateRandom(&random_address_, random_address_prefix_len_);

    neighbor_finder_.reset(new NeighborFinder());
    neighbor_finder_->Check(
        ifname, random_address_,
        base::Bind(&Device::OnNeighborCheckResult, weak_factory_.GetWeakPtr()));
  } else {
    LOG(INFO) << "No IPv6 connectivity available on " << ifname;
  }
}

void Device::OnNeighborCheckResult(bool found) {
  const std::string& ifname =
      legacy_lan_ifname_.empty() ? ifname_ : legacy_lan_ifname_;

  if (found) {
    if (++random_address_tries_ >= kMaxRandomAddressTries) {
      LOG(WARNING) << "Too many IP collisions, giving up.";
      return;
    }

    struct in6_addr previous_address = random_address_;
    ArcIpConfig::GenerateRandom(&random_address_, random_address_prefix_len_);

    LOG(INFO) << "Detected IP collision for " << previous_address
              << ", retrying with new address " << random_address_;

    neighbor_finder_->Check(
        ifname, random_address_,
        base::Bind(&Device::OnNeighborCheckResult, weak_factory_.GetWeakPtr()));
  } else {
    struct in6_addr router;

    if (!ArcIpConfig::GetV6Address(config_->host_ifname(), &router)) {
      LOG(ERROR) << "Error reading link local address for "
                 << config_->host_ifname();
      return;
    }

    LOG(INFO) << "Setting IPv6 address " << random_address_
              << "/128, gateway=" << router << " on " << ifname;

    // Set up new ARC IPv6 address, NDP, and forwarding rules.
    if (!msg_sink_.is_null()) {
      DeviceMessage msg;
      msg.set_dev_ifname(ifname_);
      SetArcIp* setup_msg = msg.mutable_set_arc_ip();
      setup_msg->set_prefix(&random_address_, sizeof(struct in6_addr));
      setup_msg->set_prefix_len(128);
      setup_msg->set_router(&router, sizeof(struct in6_addr));
      setup_msg->set_lan_ifname(ifname);
      msg_sink_.Run(msg);
    }
  }
}

std::ostream& operator<<(std::ostream& stream, const Device& device) {
  stream << "{ ifname: " << device.ifname_;
  if (!device.legacy_lan_ifname_.empty())
    stream << ", legacy_lan_ifname: " << device.legacy_lan_ifname_;
  stream << ", bridge_ifname: " << device.config_->host_ifname()
         << ", bridge_ipv4_addr: "
         << device.config_->host_ipv4_addr_->ToCidrString()
         << ", guest_ifname: " << device.config_->guest_ifname()
         << ", guest_ipv4_addr: "
         << device.config_->guest_ipv4_addr_->ToCidrString()
         << ", guest_mac_addr: "
         << MacAddressToString(device.config_->guest_mac_addr())
         << ", fwd_multicast: " << device.options_.fwd_multicast
         << ", find_ipv6_routes: " << device.options_.find_ipv6_routes << '}';
  return stream;
}

}  // namespace arc_networkd
