// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/network.h"

#include <string>
#include <vector>

#include <base/containers/contains.h>
#include <base/files/file_util.h>
#include <base/notreached.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/connection.h"
#include "shill/device_info.h"
#include "shill/event_dispatcher.h"
#include "shill/logging.h"
#include "shill/net/ndisc.h"
#include "shill/net/rtnl_handler.h"
#include "shill/routing_table.h"
#include "shill/routing_table_entry.h"
#include "shill/service.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDevice;
static std::string ObjectID(const Network* n) {
  return n->interface_name();
}
}  // namespace Logging

namespace {

constexpr char kIPFlagTemplate[] = "/proc/sys/net/%s/conf/%s/%s";
constexpr char kIPFlagVersion4[] = "ipv4";
constexpr char kIPFlagVersion6[] = "ipv6";

constexpr char kIPFlagAcceptDuplicateAddressDetection[] = "accept_dad";
constexpr char kIPFlagAcceptDuplicateAddressDetectionEnabled[] = "1";
constexpr char kIPFlagAcceptRouterAdvertisements[] = "accept_ra";
constexpr char kIPFlagAcceptRouterAdvertisementsAlways[] = "2";
constexpr char kIPFlagDisableIPv6[] = "disable_ipv6";
constexpr char kIPFlagUseTempAddr[] = "use_tempaddr";
constexpr char kIPFlagUseTempAddrUsedAndDefault[] = "2";

constexpr char kIPFlagArpAnnounce[] = "arp_announce";
constexpr char kIPFlagArpAnnounceBestLocal[] = "2";
constexpr char kIPFlagArpIgnore[] = "arp_ignore";
constexpr char kIPFlagArpIgnoreLocalOnly[] = "1";

}  // namespace

Network::Network(int interface_index,
                 const std::string& interface_name,
                 Technology technology,
                 bool fixed_ip_params,
                 EventHandler* event_handler,
                 ControlInterface* control_interface,
                 DeviceInfo* device_info,
                 EventDispatcher* dispatcher)
    : interface_index_(interface_index),
      interface_name_(interface_name),
      technology_(technology),
      fixed_ip_params_(fixed_ip_params),
      event_handler_(event_handler),
      control_interface_(control_interface),
      device_info_(device_info),
      dispatcher_(dispatcher),
      dhcp_provider_(DHCPProvider::GetInstance()),
      routing_table_(RoutingTable::GetInstance()),
      rtnl_handler_(RTNLHandler::GetInstance()) {}

Network::~Network() = default;

void Network::Start(const Network::StartOptions& opts) {
  // accept_ra and link_protocol_ipv6 should not be set at the same time.
  DCHECK(!(opts.accept_ra && link_protocol_ipv6_properties_));

  // TODO(b/232177767): Log the StartOptions and other parameters.
  if (state_ != State::kIdle) {
    LOG(INFO) << interface_name_
              << ": Network has been started, stop it before starting with the "
                 "new options";
    StopInternal(/*is_failure=*/false, /*trigger_callback=*/false);
  }

  EnableARPFiltering();

  // If the execution of this function fails, StopInternal() will be called and
  // turn the state to kIdle.
  state_ = State::kConfiguring;

  bool ipv6_started = false;
  if (opts.accept_ra) {
    StartIPv6();
    ipv6_started = true;
  }
  if (ipv6_static_properties_) {
    dispatcher_->PostTask(
        FROM_HERE,
        base::BindOnce(&Network::ConfigureStaticIPv6Address, AsWeakPtr()));
    ipv6_started = true;
  }
  if (link_protocol_ipv6_properties_) {
    StartIPv6();
    set_ip6config(
        std::make_unique<IPConfig>(control_interface_, interface_name_));
    ip6config_->set_properties(*link_protocol_ipv6_properties_);
    dispatcher_->PostTask(FROM_HERE,
                          base::BindOnce(&Network::SetupConnection, AsWeakPtr(),
                                         ip6config_.get()));
    ipv6_started = true;
  }

  // Note that currently, the existence of ipconfig_ indicates if the IPv4 part
  // of Network has been started.
  bool dhcp_started = false;
  if (opts.dhcp) {
    auto dhcp_opts = *opts.dhcp;
    if (static_network_config_.ipv4_address_cidr) {
      dhcp_opts.use_arp_gateway = false;
    }
    dhcp_controller_ = dhcp_provider_->CreateController(interface_name_,
                                                        dhcp_opts, technology_);
    dhcp_controller_->RegisterCallbacks(
        base::BindRepeating(&Network::OnIPConfigUpdatedFromDHCP, AsWeakPtr()),
        base::BindRepeating(&Network::OnDHCPFailure, AsWeakPtr()));
    set_ipconfig(std::make_unique<IPConfig>(control_interface_, interface_name_,
                                            IPConfig::kTypeDHCP));
    dhcp_started = dhcp_controller_->RequestIP();
  } else if (link_protocol_ipv4_properties_) {
    set_ipconfig(
        std::make_unique<IPConfig>(control_interface_, interface_name_));
    ipconfig_->set_properties(*link_protocol_ipv4_properties_);
  } else {
    // This could happen on IPv6-only networks.
    DCHECK(ipv6_started);
  }

  if (link_protocol_ipv4_properties_ ||
      static_network_config_.ipv4_address_cidr) {
    // If the parameters contain an IP address, apply them now and bring the
    // interface up.  When DHCP information arrives, it will supplement the
    // static information.
    dispatcher_->PostTask(
        FROM_HERE, base::BindOnce(&Network::OnIPv4ConfigUpdated, AsWeakPtr()));
  } else if (!dhcp_started && !ipv6_started) {
    // Neither v4 nor v6 is running, trigger the failure callback directly.
    dispatcher_->PostTask(
        FROM_HERE,
        base::BindOnce(&Network::StopInternal, AsWeakPtr(),
                       /*is_failure=*/true, /*trigger_callback=*/true));
  }
}

void Network::SetupConnection(IPConfig* ipconfig) {
  DCHECK(ipconfig);
  if (connection_ == nullptr) {
    connection_ = CreateConnection();
  }
  connection_->UpdateFromIPConfig(ipconfig->properties());
  state_ = State::kConnected;
  ConfigureStaticIPv6Address();
  event_handler_->OnConnectionUpdated();

  const bool ipconfig_changed = current_ipconfig_ != ipconfig;
  current_ipconfig_ = ipconfig;
  if (ipconfig_changed && !current_ipconfig_change_handler_.is_null()) {
    current_ipconfig_change_handler_.Run();
  }
}

std::unique_ptr<Connection> Network::CreateConnection() const {
  return std::make_unique<Connection>(interface_index_, interface_name_,
                                      fixed_ip_params_, technology_,
                                      device_info_);
}

void Network::Stop() {
  StopInternal(/*is_failure=*/false, /*trigger_callback=*/true);
}

void Network::StopInternal(bool is_failure, bool trigger_callback) {
  const bool should_trigger_callback =
      state_ != State::kIdle && trigger_callback;
  StopIPv6();
  bool ipconfig_changed = false;
  if (dhcp_controller_) {
    dhcp_controller_->ReleaseIP(DHCPController::kReleaseReasonDisconnect);
    dhcp_controller_ = nullptr;
  }
  if (ipconfig()) {
    set_ipconfig(nullptr);
    link_protocol_ipv4_properties_ = {};
    ipconfig_changed = true;
  }
  // Make sure the timer is stopped regardless of the ip6config state. Just in
  // case that they are not synced.
  StopIPv6DNSServerTimer();
  if (ip6config()) {
    set_ip6config(nullptr);
    link_protocol_ipv6_properties_ = {};
    ipconfig_changed = true;
  }
  // Emit updated IP configs if there are any changes.
  if (ipconfig_changed) {
    event_handler_->OnIPConfigsPropertyUpdated();
  }
  if (current_ipconfig_) {
    current_ipconfig_ = nullptr;
    if (!current_ipconfig_change_handler_.is_null()) {
      current_ipconfig_change_handler_.Run();
    }
  }
  state_ = State::kIdle;
  connection_ = nullptr;
  if (should_trigger_callback) {
    event_handler_->OnNetworkStopped(is_failure);
  }
}

void Network::InvalidateIPv6Config() {
  SLOG(this, 2) << interface_name_ << ": " << __func__;
  if (!ip6config_) {
    return;
  }

  SLOG(this, 2) << interface_name_ << "Waiting for new IPv6 configuration";
  // Invalidate the old IPv6 configuration, will receive notifications
  // from kernel for new IPv6 configuration if there is one.
  StopIPv6DNSServerTimer();
  set_ip6config(nullptr);
  event_handler_->OnIPConfigsPropertyUpdated();
}

void Network::OnIPv4ConfigUpdated() {
  if (!ipconfig()) {
    return;
  }
  saved_network_config_ =
      ipconfig()->ApplyNetworkConfig(static_network_config_);
  if (static_network_config_.ipv4_address_cidr.has_value() &&
      dhcp_controller_) {
    // If we are using a statically configured IP address instead of a leased IP
    // address, release any acquired lease so it may be used by others.  This
    // allows us to merge other non-leased parameters (like DNS) when they're
    // available from a DHCP server and not overridden by static parameters, but
    // at the same time we avoid taking up a dynamic IP address the DHCP server
    // could assign to someone else who might actually use it.
    dhcp_controller_->ReleaseIP(DHCPController::kReleaseReasonStaticIP);
  }
  SetupConnection(ipconfig());
  event_handler_->OnIPConfigsPropertyUpdated();
}

void Network::OnStaticIPConfigChanged(const NetworkConfig& config) {
  static_network_config_ = config;
  if (state_ == State::kIdle) {
    // This can happen after service is selected but before the Network starts.
    return;
  }

  // Clear the previously applied static IP parameters. The new config will be
  // applied in ConfigureStaticIPTask().
  ipconfig()->ApplyNetworkConfig(saved_network_config_);
  saved_network_config_ = {};

  // TODO(b/232177767): Apply the static IP parameters no matter if there is a
  // valid IPv4 in it.
  if (config.ipv4_address_cidr.has_value()) {
    dispatcher_->PostTask(
        FROM_HERE, base::BindOnce(&Network::OnIPv4ConfigUpdated, AsWeakPtr()));
  }

  if (dhcp_controller_) {
    // Trigger DHCP renew.
    dhcp_controller_->RenewIP();
  }
}

void Network::RegisterCurrentIPConfigChangeHandler(
    base::RepeatingClosure handler) {
  current_ipconfig_change_handler_ = handler;
}

IPConfig* Network::GetCurrentIPConfig() const {
  // Make sure that the |current_ipconfig_| is still valid.
  if (current_ipconfig_ == ipconfig_.get()) {
    return current_ipconfig_;
  }
  if (current_ipconfig_ == ip6config_.get()) {
    return current_ipconfig_;
  }
  return nullptr;
}

void Network::OnIPConfigUpdatedFromDHCP(const IPConfig::Properties& properties,
                                        bool new_lease_acquired) {
  // |dhcp_controller_| cannot be empty when the callback is invoked.
  DCHECK(dhcp_controller_);
  DCHECK(ipconfig());
  if (new_lease_acquired) {
    event_handler_->OnGetDHCPLease();
  }
  ipconfig()->UpdateProperties(properties);
  OnIPv4ConfigUpdated();
  // TODO(b/232177767): OnIPv4ConfiguredWithDHCPLease() should be called inside
  // Network::OnIPv4ConfigUpdated() and only if SetupConnection() happened as a
  // result of the new lease. The current call pattern reproduces the same
  // conditions as before crrev/c/3840983.
  if (new_lease_acquired) {
    event_handler_->OnIPv4ConfiguredWithDHCPLease();
  }
}

void Network::OnDHCPFailure() {
  event_handler_->OnGetDHCPFailure();

  // |dhcp_controller_| cannot be empty when the callback is invoked.
  DCHECK(dhcp_controller_);
  DCHECK(ipconfig());
  if (static_network_config_.ipv4_address_cidr.has_value()) {
    // Consider three cases:
    //
    // 1. We're here because DHCP failed while starting up. There
    //    are two subcases:
    //    a. DHCP has failed, and Static IP config has _not yet_
    //       completed. It's fine to do nothing, because we'll
    //       apply the static config shortly.
    //    b. DHCP has failed, and Static IP config has _already_
    //       completed. It's fine to do nothing, because we can
    //       continue to use the static config that's already
    //       been applied.
    //
    // 2. We're here because a previously valid DHCP configuration
    //    is no longer valid. There's still a static IP config,
    //    because the condition in the if clause evaluated to true.
    //    Furthermore, the static config includes an IP address for
    //    us to use.
    //
    //    The current configuration may include some DHCP
    //    parameters, overridden by any static parameters
    //    provided. We continue to use this configuration, because
    //    the only configuration element that is leased to us (IP
    //    address) will be overridden by a static parameter.
    return;
  }

  ipconfig()->ResetProperties();
  event_handler_->OnIPConfigsPropertyUpdated();

  // Fallback to IPv6 if possible.
  if (ip6config() && ip6config()->properties().HasIPAddressAndDNS()) {
    if (!connection_ || !connection_->IsIPv6()) {
      // Setup IPv6 connection is there isn's one.
      SetupConnection(ip6config());
    }
    return;
  }

  StopInternal(/*is_failure=*/true, /*trigger_callback=*/true);
}

bool Network::RenewDHCPLease() {
  if (!dhcp_controller_) {
    return false;
  }
  SLOG(this, 2) << interface_name_ << ": renewing DHCP lease";
  // If RenewIP() fails, DHCPController will output a ERROR log.
  return dhcp_controller_->RenewIP();
}

void Network::DestroyDHCPLease(const std::string& name) {
  dhcp_provider_->DestroyLease(name);
}

std::optional<base::TimeDelta> Network::TimeToNextDHCPLeaseRenewal() {
  if (!dhcp_controller_) {
    return std::nullopt;
  }
  return dhcp_controller_->TimeToLeaseExpiry();
}

void Network::StopIPv6() {
  SetIPFlag(IPAddress::kFamilyIPv6, kIPFlagDisableIPv6, "1");
}

void Network::StartIPv6() {
  SetIPFlag(IPAddress::kFamilyIPv6, kIPFlagDisableIPv6, "0");

  SetIPFlag(IPAddress::kFamilyIPv6, kIPFlagAcceptDuplicateAddressDetection,
            kIPFlagAcceptDuplicateAddressDetectionEnabled);

  // Force the kernel to accept RAs even when global IPv6 forwarding is
  // enabled.  Unfortunately this needs to be set on a per-interface basis.
  SetIPFlag(IPAddress::kFamilyIPv6, kIPFlagAcceptRouterAdvertisements,
            kIPFlagAcceptRouterAdvertisementsAlways);
}

void Network::EnableIPv6Privacy() {
  SetIPFlag(IPAddress::kFamilyIPv6, kIPFlagUseTempAddr,
            kIPFlagUseTempAddrUsedAndDefault);
}

void Network::StartIPv6DNSServerTimer(base::TimeDelta delay) {
  ipv6_dns_server_expired_callback_.Reset(base::BindOnce(
      &Network::IPv6DNSServerExpired, weak_factory_.GetWeakPtr()));
  dispatcher_->PostDelayedTask(
      FROM_HERE, ipv6_dns_server_expired_callback_.callback(), delay);
}

void Network::StopIPv6DNSServerTimer() {
  ipv6_dns_server_expired_callback_.Cancel();
}

void Network::IPv6DNSServerExpired() {
  if (!ip6config()) {
    return;
  }
  ip6config()->UpdateDNSServers(std::vector<std::string>());
  event_handler_->OnIPConfigsPropertyUpdated();
}

void Network::ConfigureStaticIPv6Address() {
  if (!ipv6_static_properties_ || ipv6_static_properties_->address.empty()) {
    return;
  }
  IPAddress local(IPAddress::kFamilyIPv6);
  if (!local.SetAddressFromString(ipv6_static_properties_->address)) {
    LOG(ERROR) << interface_name_ << ": Local address "
               << ipv6_static_properties_->address << " is invalid";
    return;
  }
  local.set_prefix(ipv6_static_properties_->subnet_prefix);
  rtnl_handler_->AddInterfaceAddress(interface_index_, local,
                                     local.GetDefaultBroadcast(),
                                     IPAddress(IPAddress::kFamilyIPv6));
}

void Network::OnIPv6AddressChanged(const IPAddress* address) {
  if (!address) {
    if (ip6config()) {
      set_ip6config(nullptr);
      event_handler_->OnIPConfigsPropertyUpdated();
      // TODO(b/232177767): We may lose the whole IP connectivity here (if there
      // is no IPv4).
    }
    return;
  }

  CHECK_EQ(address->family(), IPAddress::kFamilyIPv6);
  IPConfig::Properties properties;
  if (!address->IntoString(&properties.address)) {
    LOG(ERROR) << interface_name_
               << ": Unable to convert IPv6 address into a string";
    return;
  }
  properties.subnet_prefix = address->prefix();

  RoutingTableEntry default_route;
  if (routing_table_->GetDefaultRouteFromKernel(interface_index_,
                                                &default_route)) {
    if (!default_route.gateway.IntoString(&properties.gateway)) {
      LOG(ERROR) << interface_name_
                 << ": Unable to convert IPv6 gateway into a string";
      return;
    }
  } else {
    // The kernel normally populates the default route before it performs
    // a neighbor solicitation for the new address, so it shouldn't be
    // missing at this point.
    LOG(WARNING) << interface_name_
                 << ": No default route for global IPv6 address "
                 << properties.address;
  }

  if (!ip6config()) {
    set_ip6config(
        std::make_unique<IPConfig>(control_interface_, interface_name_));
  } else if (properties.address == ip6config()->properties().address &&
             properties.subnet_prefix ==
                 ip6config()->properties().subnet_prefix &&
             properties.gateway == ip6config()->properties().gateway) {
    SLOG(this, 2) << __func__ << " primary address for " << interface_name_
                  << " is unchanged";
    return;
  }

  properties.address_family = IPAddress::kFamilyIPv6;
  properties.method = kTypeIPv6;
  // It is possible for device to receive DNS server notification before IP
  // address notification, so preserve the saved DNS server if it exist.
  properties.dns_servers = ip6config()->properties().dns_servers;
  if (ipv6_static_properties_ &&
      !ipv6_static_properties_->dns_servers.empty()) {
    properties.dns_servers = ipv6_static_properties_->dns_servers;
  }
  ip6config()->set_properties(properties);
  event_handler_->OnGetSLAACAddress();
  event_handler_->OnIPConfigsPropertyUpdated();
  OnIPv6ConfigUpdated();
  // TODO(b/232177767): OnIPv6ConfiguredWithSLAACAddress() should be called
  // inside Network::OnIPv6ConfigUpdated() and only if SetupConnection()
  // happened as a result of the new address (ignoring IPv4 and assuming Network
  // is fully dual-stack). The current call pattern reproduces the same
  // conditions as before crrev/c/3840983.
  event_handler_->OnIPv6ConfiguredWithSLAACAddress();
}

void Network::OnIPv6ConfigUpdated() {
  if (!ip6config()) {
    LOG(WARNING) << interface_name_ << ": " << __func__
                 << " called but |ip6config_| is empty";
    return;
  }

  // Setup connection using IPv6 configuration only if the IPv6 configuration is
  // ready for connection (contained both IP address and DNS servers), and there
  // is no existing IPv4 connection. We always prefer IPv4 configuration over
  // IPv6.
  if (ip6config()->properties().HasIPAddressAndDNS() &&
      (!IsConnected() || connection_->IsIPv6())) {
    SetupConnection(ip6config());
  }
}

void Network::OnIPv6DnsServerAddressesChanged() {
  std::vector<IPAddress> server_addresses;
  uint32_t lifetime = 0;

  // Stop any existing timer.
  StopIPv6DNSServerTimer();

  if (!device_info_->GetIPv6DnsServerAddresses(interface_index_,
                                               &server_addresses, &lifetime) ||
      lifetime == 0) {
    IPv6DNSServerExpired();
    return;
  }

  std::vector<std::string> addresses_str;
  for (const auto& ip : server_addresses) {
    std::string address_str;
    if (!ip.IntoString(&address_str)) {
      LOG(ERROR) << interface_name_
                 << ": Unable to convert IPv6 address into a string!";
      IPv6DNSServerExpired();
      return;
    }
    addresses_str.push_back(address_str);
  }

  if (!ip6config()) {
    set_ip6config(
        std::make_unique<IPConfig>(control_interface_, interface_name_));
  }

  if (lifetime != ND_OPT_LIFETIME_INFINITY) {
    // Setup timer to monitor DNS server lifetime if not infinite lifetime.
    base::TimeDelta delay = base::Seconds(lifetime);
    StartIPv6DNSServerTimer(delay);
  }

  // Done if no change in server addresses.
  if (ip6config()->properties().dns_servers == addresses_str) {
    SLOG(this, 2) << __func__ << " IPv6 DNS server list for " << interface_name_
                  << " is unchanged.";
    return;
  }

  ip6config()->UpdateDNSServers(std::move(addresses_str));
  event_handler_->OnIPConfigsPropertyUpdated();
  OnIPv6ConfigUpdated();
}

void Network::EnableARPFiltering() {
  SetIPFlag(IPAddress::kFamilyIPv4, kIPFlagArpAnnounce,
            kIPFlagArpAnnounceBestLocal);
  SetIPFlag(IPAddress::kFamilyIPv4, kIPFlagArpIgnore,
            kIPFlagArpIgnoreLocalOnly);
}

bool Network::SetIPFlag(IPAddress::Family family,
                        const std::string& flag,
                        const std::string& value) {
  std::string ip_version;
  if (family == IPAddress::kFamilyIPv4) {
    ip_version = kIPFlagVersion4;
  } else if (family == IPAddress::kFamilyIPv6) {
    ip_version = kIPFlagVersion6;
  } else {
    NOTIMPLEMENTED();
  }
  base::FilePath flag_file(
      base::StringPrintf(kIPFlagTemplate, ip_version.c_str(),
                         interface_name_.c_str(), flag.c_str()));
  if (base::WriteFile(flag_file, value.c_str(), value.length()) != 1) {
    const auto message =
        base::StringPrintf("IP flag write failed: %s to %s", value.c_str(),
                           flag_file.value().c_str());
    if (base::PathExists(flag_file) ||
        !base::Contains(written_flags_, flag_file.value())) {
      // Leave a log if the file is there or this is the first time we try to
      // write it on a failure.
      LOG(ERROR) << interface_name_ << ": " << message;
    }
    return false;
  } else {
    written_flags_.insert(flag_file.value());
  }
  return true;
}

void Network::SetPriority(uint32_t priority, bool is_primary_physical) {
  if (!connection_) {
    LOG(WARNING) << __func__ << " called but no connection exists";
    return;
  }
  connection_->SetPriority(priority, is_primary_physical);
}

bool Network::IsDefault() const {
  if (!connection_) {
    return false;
  }
  return connection_->IsDefault();
}

void Network::SetUseDNS(bool enable) {
  if (!connection_) {
    LOG(WARNING) << __func__ << " called but no connection exists";
    return;
  }
  connection_->SetUseDNS(enable);
}

void Network::UpdateRoutingPolicy() {
  if (!connection_) {
    LOG(WARNING) << __func__ << " called but no connection exists";
    return;
  }
  connection_->UpdateRoutingPolicy();
}

std::vector<std::string> Network::dns_servers() const {
  if (!connection_) {
    return {};
  }
  return connection_->dns_servers();
}

IPAddress Network::local() const {
  if (!connection_) {
    return {};
  }
  return connection_->local();
}

IPAddress Network::gateway() const {
  if (!connection_) {
    return {};
  }
  return connection_->gateway();
}

}  // namespace shill
