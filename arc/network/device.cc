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

#include "arc/network/net_util.h"

namespace arc_networkd {

// These are used to identify which ARC++ data path should be used when setting
// up the Android device.
const char kAndroidDevice[] = "arc0";
const char kAndroidLegacyDevice[] = "android";
const char kAndroidVmDevice[] = "arcvm";

namespace {
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

void Device::IPv6Config::clear() {
  memset(&addr, 0, sizeof(struct in6_addr));
  memset(&router, 0, sizeof(struct in6_addr));
  prefix_len = 0;
  addr_attempts = 0;
}

Device::Device(const std::string& ifname,
               std::unique_ptr<Device::Config> config,
               const Device::Options& options,
               GuestMessage::GuestType guest)
    : ifname_(ifname),
      config_(std::move(config)),
      options_(options),
      guest_(guest),
      host_link_up_(false) {
  DCHECK(config_);
}

const std::string& Device::ifname() const {
  return ifname_;
}

Device::Config& Device::config() const {
  CHECK(config_);
  return *config_.get();
}

Device::IPv6Config& Device::ipv6_config() {
  return ipv6_config_;
}

const Device::Options& Device::options() const {
  return options_;
}

void Device::set_context(std::unique_ptr<Device::Context> ctx) {
  ctx_ = std::move(ctx);
}

Device::Context* Device::context() {
  return ctx_.get();
}

bool Device::IsAndroid() const {
  return options_.is_android;
}

bool Device::IsArc() const {
  return guest_ == GuestMessage::ARC;
}

bool Device::UsesDefaultInterface() const {
  return options_.use_default_interface;
}

bool Device::HostLinkUp(bool link_up) {
  if (link_up == host_link_up_)
    return false;

  host_link_up_ = link_up;
  return true;
}

bool Device::IsFullyUp() const {
  if (!host_link_up_)
    return false;

  return ctx_->IsLinkUp();
}

void Device::Enable(const std::string& ifname) {
  if (!IsFullyUp())
    return;

  if (options_.ipv6_enabled && options_.find_ipv6_routes_legacy)
    StartIPv6RoutingLegacy(ifname);
}

void Device::StartIPv6RoutingLegacy(const std::string& ifname) {
  if (!IsFullyUp())
    return;

  if (router_finder_)
    return;

  LOG(INFO) << "Starting IPV6 route finding for device " << ifname_
            << " on interface " << ifname;
  // In the case this is the Android device, |ifname| is the current default
  // interface and must be used.
  ipv6_config_.ifname = IsAndroid() ? ifname : ifname_;
  ipv6_config_.addr_attempts = 0;
  router_finder_.reset(new RouterFinder());
  router_finder_->Start(
      ifname, base::Bind(&Device::OnRouteFound, weak_factory_.GetWeakPtr()));
}

void Device::Disable() {
  if (options_.ipv6_enabled && options_.find_ipv6_routes_legacy)
    StopIPv6RoutingLegacy();
}

void Device::StopIPv6RoutingLegacy() {
  if (neighbor_finder_ || router_finder_) {
    LOG(INFO) << "Disabling IPv6 route finding for device " << ifname_;
    neighbor_finder_.reset();
    router_finder_.reset();
  }

  if (!ipv6_down_handler_.is_null())
    ipv6_down_handler_.Run(this);

  ipv6_config_.clear();
}

void Device::RegisterIPv6SetupHandler(const DeviceHandler& handler) {
  ipv6_up_handler_ = handler;
}

void Device::RegisterIPv6TeardownHandler(const DeviceHandler& handler) {
  ipv6_down_handler_ = handler;
}

void Device::OnGuestStart(GuestMessage::GuestType guest) {
  host_link_up_ = false;
}

void Device::OnGuestStop(GuestMessage::GuestType guest) {}

void Device::OnRouteFound(const struct in6_addr& prefix,
                          int prefix_len,
                          const struct in6_addr& router) {
  if (prefix_len != 64) {
    LOG(INFO) << "No IPv6 connectivity available on " << ipv6_config_.ifname
              << " - unsupported prefix length: " << prefix_len;
    return;
  }

  LOG(INFO) << "Found IPv6 network on iface " << ipv6_config_.ifname
            << " route=" << prefix << "/" << prefix_len
            << ", gateway=" << router;

  memcpy(&ipv6_config_.addr, &prefix, sizeof(ipv6_config_.addr));
  ipv6_config_.prefix_len = prefix_len;

  GenerateRandomIPv6Prefix(&ipv6_config_.addr, ipv6_config_.prefix_len);

  neighbor_finder_.reset(new NeighborFinder());
  neighbor_finder_->Check(
      ipv6_config_.ifname, ipv6_config_.addr,
      base::Bind(&Device::OnNeighborCheckResult, weak_factory_.GetWeakPtr()));
}

void Device::OnNeighborCheckResult(bool found) {
  if (found) {
    if (++ipv6_config_.addr_attempts >= kMaxRandomAddressTries) {
      LOG(WARNING) << "Too many IPv6 collisions, giving up.";
      return;
    }

    struct in6_addr previous_address = ipv6_config_.addr;
    GenerateRandomIPv6Prefix(&ipv6_config_.addr, ipv6_config_.prefix_len);

    LOG(INFO) << "Detected IP collision for " << previous_address
              << ", retrying with new address " << ipv6_config_.addr;

    neighbor_finder_->Check(
        ipv6_config_.ifname, ipv6_config_.addr,
        base::Bind(&Device::OnNeighborCheckResult, weak_factory_.GetWeakPtr()));
    return;
  }

  if (!FindFirstIPv6Address(config_->host_ifname(), &ipv6_config_.router)) {
    LOG(ERROR) << "Error reading link local address for "
               << config_->host_ifname();
    return;
  }

  if (!ipv6_up_handler_.is_null())
    ipv6_up_handler_.Run(this);
}

std::ostream& operator<<(std::ostream& stream, const Device& device) {
  stream << "{ ifname: " << device.ifname_
         << ", bridge_ifname: " << device.config_->host_ifname()
         << ", bridge_ipv4_addr: "
         << device.config_->host_ipv4_addr_->ToCidrString()
         << ", guest_ifname: " << device.config_->guest_ifname()
         << ", guest_ipv4_addr: "
         << device.config_->guest_ipv4_addr_->ToCidrString()
         << ", guest_mac_addr: "
         << MacAddressToString(device.config_->guest_mac_addr())
         << ", fwd_multicast: " << device.options_.fwd_multicast
         << ", ipv6_enabled: " << device.options_.ipv6_enabled
         << ", find_ipv6_routes: " << device.options_.find_ipv6_routes_legacy
         << '}';
  return stream;
}

}  // namespace arc_networkd
