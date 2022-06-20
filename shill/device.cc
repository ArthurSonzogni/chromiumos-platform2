// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/device.h"

#include <errno.h>
#include <netinet/in.h>
#include <linux/if.h>  // NOLINT - Needs definitions from netinet/in.h
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/containers/contains.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/notreached.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/control_interface.h"
#include "shill/error.h"
#include "shill/event_dispatcher.h"
#include "shill/icmp.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/metrics.h"
#include "shill/net/ip_address.h"
#include "shill/net/ndisc.h"
#include "shill/net/rtnl_handler.h"
#include "shill/network/dhcp_controller.h"
#include "shill/network/dhcp_provider.h"
#include "shill/network/network.h"
#include "shill/refptr_types.h"
#include "shill/routing_table.h"
#include "shill/routing_table_entry.h"
#include "shill/service.h"
#include "shill/store/property_accessor.h"
#include "shill/store/store_interface.h"
#include "shill/technology.h"
#include "shill/tethering.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDevice;
static std::string ObjectID(const Device* d) {
  return d->GetRpcIdentifier().value();
}
}  // namespace Logging

namespace {

constexpr char kIPFlagTemplate[] = "/proc/sys/net/%s/conf/%s/%s";
constexpr char kIPFlagVersion4[] = "ipv4";
constexpr char kIPFlagVersion6[] = "ipv6";
constexpr char kIPFlagUseTempAddr[] = "use_tempaddr";
constexpr char kIPFlagUseTempAddrUsedAndDefault[] = "2";
constexpr char kIPFlagAcceptRouterAdvertisementsAlways[] = "2";
constexpr char kIPFlagAcceptDuplicateAddressDetectionEnabled[] = "1";
constexpr char kIPFlagArpAnnounce[] = "arp_announce";
constexpr char kIPFlagArpAnnounceDefault[] = "0";
constexpr char kIPFlagArpAnnounceBestLocal[] = "2";
constexpr char kIPFlagArpIgnore[] = "arp_ignore";
constexpr char kIPFlagArpIgnoreDefault[] = "0";
constexpr char kIPFlagArpIgnoreLocalOnly[] = "1";
constexpr size_t kHardwareAddressLength = 6;

}  // namespace

const char Device::kIPFlagDisableIPv6[] = "disable_ipv6";
const char Device::kIPFlagAcceptRouterAdvertisements[] = "accept_ra";
const char Device::kIPFlagAcceptDuplicateAddressDetection[] = "accept_dad";
const char Device::kStoragePowered[] = "Powered";

Device::Device(Manager* manager,
               const std::string& link_name,
               const std::string& mac_address,
               int interface_index,
               Technology technology)
    : enabled_(false),
      enabled_persistent_(true),
      enabled_pending_(enabled_),
      mac_address_(base::ToLowerASCII(mac_address)),
      interface_index_(interface_index),
      link_name_(link_name),
      manager_(manager),
      network_(new Network()),
      adaptor_(manager->control_interface()->CreateDeviceAdaptor(this)),
      technology_(technology),
      dhcp_provider_(DHCPProvider::GetInstance()),
      routing_table_(RoutingTable::GetInstance()),
      rtnl_handler_(RTNLHandler::GetInstance()),
      is_multi_homed_(false),
      fixed_ip_params_(false),
      traffic_counter_callback_id_(0),
      weak_ptr_factory_(this) {
  store_.RegisterConstString(kAddressProperty, &mac_address_);

  // kBgscanMethodProperty: Registered in WiFi
  // kBgscanShortIntervalProperty: Registered in WiFi
  // kBgscanSignalThresholdProperty: Registered in WiFi

  // kCellularAllowRoamingProperty: Registered in Cellular
  // kEsnProperty: Registered in Cellular
  // kHomeProviderProperty: Registered in Cellular
  // kImeiProperty: Registered in Cellular
  // kIccidProperty: Registered in Cellular
  // kImsiProperty: Registered in Cellular
  // kInhibit: Registered in Cellular
  // kManufacturerProperty: Registered in Cellular
  // kMdnProperty: Registered in Cellular
  // kMeidProperty: Registered in Cellular
  // kMinProperty: Registered in Cellular
  // kModelIdProperty: Registered in Cellular
  // kFirmwareRevisionProperty: Registered in Cellular
  // kHardwareRevisionProperty: Registered in Cellular
  // kDeviceIdProperty: Registered in Cellular
  // kSIMLockStatusProperty: Registered in Cellular
  // kFoundNetworksProperty: Registered in Cellular
  // kDBusObjectProperty: Register in Cellular
  // kUseAttachAPNProperty: Registered in Cellular

  store_.RegisterConstString(kInterfaceProperty, &link_name_);
  HelpRegisterConstDerivedRpcIdentifier(
      kSelectedServiceProperty, &Device::GetSelectedServiceRpcIdentifier);
  HelpRegisterConstDerivedRpcIdentifiers(kIPConfigsProperty,
                                         &Device::AvailableIPConfigs);
  store_.RegisterConstString(kNameProperty, &link_name_);
  store_.RegisterConstBool(kPoweredProperty, &enabled_);
  HelpRegisterConstDerivedString(kTypeProperty, &Device::GetTechnologyString);

  // kScanningProperty: Registered in WiFi, Cellular
  // kScanIntervalProperty: Registered in WiFi, Cellular
  // kWakeOnWiFiFeaturesEnabledProperty: Registered in WiFi

  SLOG(this, 1) << "Device(): " << link_name_ << " index: " << interface_index_;
}

Device::~Device() {
  SLOG(this, 1) << "~Device(): " << link_name_
                << " index: " << interface_index_;
}

void Device::Initialize() {
  SLOG(this, 2) << "Initialized";
  DisableArpFiltering();
}

void Device::LinkEvent(unsigned flags, unsigned change) {
  SLOG(this, 2) << base::StringPrintf("Device %s flags 0x%x changed 0x%x",
                                      link_name_.c_str(), flags, change);
}

void Device::Scan(Error* error, const std::string& reason) {
  SLOG(this, 2) << __func__ << " [Device] on " << link_name() << " from "
                << reason;
  Error::PopulateAndLog(
      FROM_HERE, error, Error::kNotImplemented,
      technology().GetName() + " device doesn't implement Scan");
}

void Device::RegisterOnNetwork(const std::string& /*network_id*/,
                               Error* error,
                               const ResultCallback& /*callback*/) {
  Error::PopulateAndLog(
      FROM_HERE, error, Error::kNotImplemented,
      technology().GetName() + " device doesn't implement RegisterOnNetwork");
}

void Device::RequirePin(const std::string& /*pin*/,
                        bool /*require*/,
                        Error* error,
                        const ResultCallback& /*callback*/) {
  SLOG(this, 2) << __func__;
  Error::PopulateAndLog(
      FROM_HERE, error, Error::kNotImplemented,
      technology().GetName() + " device doesn't implement RequirePin");
}

void Device::EnterPin(const std::string& /*pin*/,
                      Error* error,
                      const ResultCallback& /*callback*/) {
  SLOG(this, 2) << __func__;
  Error::PopulateAndLog(
      FROM_HERE, error, Error::kNotImplemented,
      technology().GetName() + " device doesn't implement EnterPin");
}

void Device::UnblockPin(const std::string& /*unblock_code*/,
                        const std::string& /*pin*/,
                        Error* error,
                        const ResultCallback& /*callback*/) {
  SLOG(this, 2) << __func__;
  Error::PopulateAndLog(
      FROM_HERE, error, Error::kNotImplemented,
      technology().GetName() + " device doesn't implement UnblockPin");
}

void Device::ChangePin(const std::string& /*old_pin*/,
                       const std::string& /*new_pin*/,
                       Error* error,
                       const ResultCallback& /*callback*/) {
  SLOG(this, 2) << __func__;
  Error::PopulateAndLog(
      FROM_HERE, error, Error::kNotImplemented,
      technology().GetName() + " device doesn't implement ChangePin");
}

void Device::Reset(Error* error, const ResultCallback& /*callback*/) {
  SLOG(this, 2) << __func__;
  Error::PopulateAndLog(
      FROM_HERE, error, Error::kNotImplemented,
      technology().GetName() + " device doesn't implement Reset");
}

void Device::StopIPv6() {
  SLOG(this, 2) << __func__;
  SetIPFlag(IPAddress::kFamilyIPv6, kIPFlagDisableIPv6, "1");
}

void Device::StartIPv6() {
  SLOG(this, 2) << __func__;
  SetIPFlag(IPAddress::kFamilyIPv6, kIPFlagDisableIPv6, "0");

  SetIPFlag(IPAddress::kFamilyIPv6, kIPFlagAcceptDuplicateAddressDetection,
            kIPFlagAcceptDuplicateAddressDetectionEnabled);

  // Force the kernel to accept RAs even when global IPv6 forwarding is
  // enabled.  Unfortunately this needs to be set on a per-interface basis.
  SetIPFlag(IPAddress::kFamilyIPv6, kIPFlagAcceptRouterAdvertisements,
            kIPFlagAcceptRouterAdvertisementsAlways);
}

void Device::EnableIPv6Privacy() {
  SetIPFlag(IPAddress::kFamilyIPv6, kIPFlagUseTempAddr,
            kIPFlagUseTempAddrUsedAndDefault);
}

void Device::SetIsMultiHomed(bool is_multi_homed) {
  if (is_multi_homed == is_multi_homed_) {
    return;
  }
  LOG(INFO) << "Device " << link_name() << " multi-home state is now "
            << is_multi_homed;
  is_multi_homed_ = is_multi_homed;
  if (is_multi_homed) {
    EnableArpFiltering();
  } else {
    DisableArpFiltering();
  }
}

void Device::SetFixedIpParams(bool fixed_ip_params) {
  fixed_ip_params_ = fixed_ip_params;
}

void Device::DisableArpFiltering() {
  SetIPFlag(IPAddress::kFamilyIPv4, kIPFlagArpAnnounce,
            kIPFlagArpAnnounceDefault);
  SetIPFlag(IPAddress::kFamilyIPv4, kIPFlagArpIgnore, kIPFlagArpIgnoreDefault);
}

void Device::EnableArpFiltering() {
  SetIPFlag(IPAddress::kFamilyIPv4, kIPFlagArpAnnounce,
            kIPFlagArpAnnounceBestLocal);
  SetIPFlag(IPAddress::kFamilyIPv4, kIPFlagArpIgnore,
            kIPFlagArpIgnoreLocalOnly);
}

bool Device::IsConnected() const {
  if (selected_service_)
    return selected_service_->IsConnected();
  return false;
}

bool Device::IsConnectedToService(const ServiceRefPtr& service) const {
  return service == selected_service_ && IsConnected();
}

bool Device::IsConnectedViaTether() const {
  if (!ipconfig_)
    return false;

  ByteArray vendor_encapsulated_options =
      ipconfig_->properties().vendor_encapsulated_options;
  size_t android_vendor_encapsulated_options_len =
      strlen(Tethering::kAndroidVendorEncapsulatedOptions);

  return (vendor_encapsulated_options.size() ==
          android_vendor_encapsulated_options_len) &&
         !memcmp(&vendor_encapsulated_options[0],
                 Tethering::kAndroidVendorEncapsulatedOptions,
                 vendor_encapsulated_options.size());
}

void Device::OnSelectedServiceChanged(const ServiceRefPtr&) {}

const RpcIdentifier& Device::GetRpcIdentifier() const {
  return adaptor_->GetRpcIdentifier();
}

std::string Device::GetStorageIdentifier() const {
  return "device_" + mac_address_;
}

std::vector<GeolocationInfo> Device::GetGeolocationObjects() const {
  return std::vector<GeolocationInfo>();
}

std::string Device::GetTechnologyString(Error* /*error*/) {
  return technology().GetName();
}

const std::string& Device::UniqueName() const {
  return link_name_;
}

bool Device::Load(const StoreInterface* storage) {
  const auto id = GetStorageIdentifier();
  if (!storage->ContainsGroup(id)) {
    SLOG(this, 2) << "Device is not available in the persistent store: " << id;
    return false;
  }
  enabled_persistent_ = true;
  storage->GetBool(id, kStoragePowered, &enabled_persistent_);
  return true;
}

bool Device::Save(StoreInterface* storage) {
  const auto id = GetStorageIdentifier();
  storage->SetBool(id, kStoragePowered, enabled_persistent_);
  return true;
}

void Device::OnBeforeSuspend(const ResultCallback& callback) {
  // Nothing to be done in the general case, so immediately report success.
  callback.Run(Error(Error::kSuccess));
}

void Device::OnAfterResume() {
  RenewDHCPLease(false, nullptr);
}

void Device::OnDarkResume(const ResultCallback& callback) {
  // Nothing to be done in the general case, so immediately report success.
  callback.Run(Error(Error::kSuccess));
}

void Device::DropConnection() {
  SLOG(this, 2) << __func__;
  DestroyIPConfig();
  SelectService(nullptr);
}

void Device::ResetConnection() {
  SLOG(this, 2) << __func__;
  DestroyIPConfig();
  if (!selected_service_) {
    return;
  }

  // Refresh traffic counters before deselecting the service.
  FetchTrafficCounters(selected_service_, /*new_service=*/nullptr);
  const ServiceRefPtr old_service = selected_service_;
  selected_service_ = nullptr;
  OnSelectedServiceChanged(old_service);
  adaptor_->EmitRpcIdentifierChanged(kSelectedServiceProperty,
                                     GetSelectedServiceRpcIdentifier(nullptr));
}

void Device::DestroyIPConfig() {
  StopIPv6();
  bool ipconfig_changed = false;
  if (dhcp_controller_) {
    dhcp_controller_->ReleaseIP(DHCPController::kReleaseReasonDisconnect);
    dhcp_controller_ = nullptr;
  }
  if (ipconfig_) {
    ipconfig_ = nullptr;
    ipconfig_changed = true;
  }
  if (ip6config_) {
    StopIPv6DNSServerTimer();
    ip6config_ = nullptr;
    ipconfig_changed = true;
  }
  // Emit updated IP configs if there are any changes.
  if (ipconfig_changed) {
    UpdateIPConfigsProperty();
  }
  DestroyConnection();
}

void Device::OnIPv6AddressChanged(const IPAddress* address) {
  if (!address) {
    if (ip6config_) {
      ip6config_ = nullptr;
      UpdateIPConfigsProperty();
    }
    return;
  }

  CHECK_EQ(address->family(), IPAddress::kFamilyIPv6);
  IPConfig::Properties properties;
  if (!address->IntoString(&properties.address)) {
    LOG(ERROR) << "Unable to convert IPv6 address into a string";
    return;
  }
  properties.subnet_prefix = address->prefix();

  RoutingTableEntry default_route;
  if (routing_table_->GetDefaultRouteFromKernel(interface_index_,
                                                &default_route)) {
    if (!default_route.gateway.IntoString(&properties.gateway)) {
      LOG(ERROR) << "Unable to convert IPv6 gateway into a string";
      return;
    }
  } else {
    // The kernel normally populates the default route before it performs
    // a neighbor solicitation for the new address, so it shouldn't be
    // missing at this point.
    LOG(WARNING) << "No default route for global IPv6 address "
                 << properties.address;
  }

  if (!ip6config_) {
    ip6config_ = std::make_unique<IPConfig>(control_interface(), link_name_);
  } else if (properties.address == ip6config_->properties().address &&
             properties.subnet_prefix ==
                 ip6config_->properties().subnet_prefix &&
             properties.gateway == ip6config_->properties().gateway) {
    SLOG(this, 2) << __func__ << " primary address for " << link_name_
                  << " is unchanged";
    return;
  }

  properties.address_family = IPAddress::kFamilyIPv6;
  properties.method = kTypeIPv6;
  // It is possible for device to receive DNS server notification before IP
  // address notification, so preserve the saved DNS server if it exist.
  properties.dns_servers = ip6config_->properties().dns_servers;
  if (ipv6_static_properties_ &&
      !ipv6_static_properties_->dns_servers.empty()) {
    properties.dns_servers = ipv6_static_properties_->dns_servers;
  }
  ip6config_->set_properties(properties);
  UpdateIPConfigsProperty();
  OnIPv6ConfigUpdated();
  OnGetSLAACAddress();
}

void Device::OnIPv6DnsServerAddressesChanged() {
  std::vector<IPAddress> server_addresses;
  uint32_t lifetime = 0;

  // Stop any existing timer.
  StopIPv6DNSServerTimer();

  if (!manager_->device_info()->GetIPv6DnsServerAddresses(
          interface_index_, &server_addresses, &lifetime) ||
      lifetime == 0) {
    IPv6DNSServerExpired();
    return;
  }

  std::vector<std::string> addresses_str;
  for (const auto& ip : server_addresses) {
    std::string address_str;
    if (!ip.IntoString(&address_str)) {
      LOG(ERROR) << "Unable to convert IPv6 address into a string!";
      IPv6DNSServerExpired();
      return;
    }
    addresses_str.push_back(address_str);
  }

  if (!ip6config_) {
    ip6config_ = std::make_unique<IPConfig>(control_interface(), link_name_);
  }

  if (lifetime != ND_OPT_LIFETIME_INFINITY) {
    // Setup timer to monitor DNS server lifetime if not infinite lifetime.
    base::TimeDelta delay = base::Seconds(lifetime);
    StartIPv6DNSServerTimer(delay);
  }

  // Done if no change in server addresses.
  if (ip6config_->properties().dns_servers == addresses_str) {
    SLOG(this, 2) << __func__ << " IPv6 DNS server list for " << link_name_
                  << " is unchanged.";
    return;
  }

  ip6config_->UpdateDNSServers(std::move(addresses_str));
  UpdateIPConfigsProperty();
  OnIPv6ConfigUpdated();
}

void Device::StartIPv6DNSServerTimer(base::TimeDelta delay) {
  ipv6_dns_server_expired_callback_.Reset(base::Bind(
      &Device::IPv6DNSServerExpired, weak_ptr_factory_.GetWeakPtr()));
  dispatcher()->PostDelayedTask(
      FROM_HERE, ipv6_dns_server_expired_callback_.callback(), delay);
}

void Device::StopIPv6DNSServerTimer() {
  ipv6_dns_server_expired_callback_.Cancel();
}

void Device::IPv6DNSServerExpired() {
  if (!ip6config_) {
    return;
  }
  ip6config_->UpdateDNSServers(std::vector<std::string>());
  UpdateIPConfigsProperty();
}

void Device::StopAllActivities() {
  StopPortalDetection();
  StopConnectionDiagnostics();
  StopIPv6DNSServerTimer();
}

void Device::SetUsbEthernetMacAddressSource(const std::string& source,
                                            Error* error,
                                            const ResultCallback& callback) {
  Error::PopulateAndLog(FROM_HERE, error, Error::kNotImplemented,
                        "SetUsbEthernetMacAddressSource from source " + source +
                            " is not implemented for " +
                            technology().GetName() + " device on " +
                            link_name_ + ".");
  return;
}

void Device::RenewDHCPLease(bool from_dbus, Error* /*error*/) {
  LOG(INFO) << __func__;

  if (dhcp_controller_) {
    SLOG(this, 3) << "Renewing IPv4 Address";
    dhcp_controller_->RenewIP();
  }
  if (ip6config_ && !from_dbus) {
    SLOG(this, 3) << "Waiting for new IPv6 configuration";
    // Invalidate the old IPv6 configuration, will receive notifications
    // from kernel for new IPv6 configuration if there is one.
    StopIPv6DNSServerTimer();
    ip6config_ = nullptr;
    UpdateIPConfigsProperty();
  }
}

bool Device::ShouldUseArpGateway() const {
  return false;
}

bool Device::IsUsingStaticIP() const {
  if (!selected_service_) {
    return false;
  }
  return selected_service_->HasStaticIPAddress();
}

bool Device::IsUsingStaticNameServers() const {
  if (!selected_service_) {
    return false;
  }
  return selected_service_->HasStaticNameServers();
}

bool Device::AcquireIPConfig() {
  return AcquireIPConfigWithLeaseName(std::string());
}

bool Device::AcquireIPConfigWithLeaseName(const std::string& lease_name) {
  DestroyIPConfig();
  StartIPv6();
  bool arp_gateway = manager_->GetArpGateway() && ShouldUseArpGateway();
  dhcp_controller_ =
      dhcp_provider_->CreateController(link_name_, lease_name, arp_gateway,
                                       manager_->dhcp_hostname(), technology_);
  const int minimum_mtu = manager()->GetMinimumMTU();
  if (minimum_mtu != IPConfig::kUndefinedMTU) {
    dhcp_controller_->set_minimum_mtu(minimum_mtu);
  }

  dhcp_controller_->RegisterCallbacks(
      base::BindRepeating(&Device::OnIPConfigUpdatedFromDHCP, AsWeakPtr()),
      base::BindRepeating(&Device::OnDHCPFailure, AsWeakPtr()));
  ipconfig_ = std::make_unique<IPConfig>(control_interface(), link_name_,
                                         IPConfig::kTypeDHCP);
  dispatcher()->PostTask(
      FROM_HERE, base::BindOnce(&Device::ConfigureStaticIPTask, AsWeakPtr()));
  return dhcp_controller_->RequestIP();
}

void Device::UpdateBlackholeUserTraffic() {
  SLOG(this, 2) << __func__;
  if (ipconfig_) {
    bool updated;
    if (manager_->ShouldBlackholeUserTraffic(UniqueName())) {
      updated = ipconfig_->SetBlackholedUids(manager_->GetUserTrafficUids());
    } else {
      updated = ipconfig_->ClearBlackholedUids();
    }
    if (updated) {
      SetupConnection(ipconfig_.get());
    }
  }
}

void Device::FetchTrafficCounters(const ServiceRefPtr& old_service,
                                  const ServiceRefPtr& new_service) {
  std::set<std::string> devices{link_name_};
  patchpanel::Client* client = manager_->patchpanel_client();
  if (!client) {
    return;
  }
  traffic_counter_callback_id_++;
  traffic_counters_callback_map_[traffic_counter_callback_id_] =
      base::BindOnce(&Device::GetTrafficCountersCallback, AsWeakPtr(),
                     old_service, new_service);
  client->GetTrafficCounters(
      devices, base::BindOnce(&Device::GetTrafficCountersPatchpanelCallback,
                              AsWeakPtr(), traffic_counter_callback_id_));
}

void Device::OnNeighborReachabilityEvent(
    const IPAddress& ip_address,
    patchpanel::NeighborReachabilityEventSignal::Role role,
    patchpanel::NeighborReachabilityEventSignal::EventType event_type) {
  // Does nothing in the general case.
}

void Device::AssignIPConfig(const IPConfig::Properties& properties) {
  DestroyIPConfig();
  StartIPv6();
  ipconfig_ = std::make_unique<IPConfig>(control_interface(), link_name_);
  ipconfig_->set_properties(properties);
  dispatcher()->PostTask(
      FROM_HERE, base::BindOnce(&Device::OnIPv4ConfigUpdated, AsWeakPtr()));
}

void Device::AssignStaticIPv6Config(const IPConfig::Properties& properties) {
  StartIPv6();

  // Only apply static config if the address is link local. This is a workaround
  // for b/230336493.
  IPAddress link_local_mask("fe80::", 10);
  if (!link_local_mask.CanReachAddress(IPAddress(properties.address))) {
    return;
  }

  ipv6_static_properties_ = properties;
  dispatcher()->PostTask(
      FROM_HERE,
      base::BindOnce(&Device::ConfigureStaticIPv6Address, AsWeakPtr()));
  // UpdateIPConfigsProperty() will be called later when SLAAC finishes, that
  // is also where static DNS configuration will be applied.
}

void Device::DestroyIPConfigLease(const std::string& name) {
  dhcp_provider_->DestroyLease(name);
}

void Device::HelpRegisterConstDerivedString(
    const std::string& name, std::string (Device::*get)(Error* error)) {
  store_.RegisterDerivedString(
      name, StringAccessor(
                new CustomAccessor<Device, std::string>(this, get, nullptr)));
}

void Device::HelpRegisterConstDerivedRpcIdentifier(
    const std::string& name, RpcIdentifier (Device::*get)(Error* error)) {
  store_.RegisterDerivedRpcIdentifier(
      name, RpcIdentifierAccessor(
                new CustomAccessor<Device, RpcIdentifier>(this, get, nullptr)));
}

void Device::HelpRegisterConstDerivedRpcIdentifiers(
    const std::string& name, RpcIdentifiers (Device::*get)(Error*)) {
  store_.RegisterDerivedRpcIdentifiers(
      name, RpcIdentifiersAccessor(new CustomAccessor<Device, RpcIdentifiers>(
                this, get, nullptr)));
}

void Device::HelpRegisterConstDerivedUint64(const std::string& name,
                                            uint64_t (Device::*get)(Error*)) {
  store_.RegisterDerivedUint64(
      name,
      Uint64Accessor(new CustomAccessor<Device, uint64_t>(this, get, nullptr)));
}

void Device::ConfigureStaticIPTask() {
  SLOG(this, 2) << __func__ << " selected_service " << selected_service_.get()
                << " ipconfig " << ipconfig_.get();

  if (!selected_service_ || !ipconfig_) {
    return;
  }

  if (IsUsingStaticIP()) {
    SLOG(this, 2) << __func__ << " "
                  << " configuring static IP parameters.";
    // If the parameters contain an IP address, apply them now and bring
    // the interface up.  When DHCP information arrives, it will supplement
    // the static information.
    OnIPv4ConfigUpdated();
  } else {
    // Either |ipconfig_| has just been created in AcquireIPConfig() or
    // we're being called by OnIPConfigRefreshed().  In either case a
    // DHCP client has been started, and will take care of calling
    // OnIPv4ConfigUpdated() when it completes.
    SLOG(this, 2) << __func__ << " "
                  << " no static IP address.";
  }
}

void Device::OnIPv6ConfigUpdated() {
  if (!ip6config_) {
    LOG(WARNING) << __func__ << " called but |ip6config_| is empty";
    return;
  }

  // Setup connection using IPv6 configuration only if the IPv6 configuration
  // is ready for connection (contained both IP address and DNS servers), and
  // there is no existing IPv4 connection. We always prefer IPv4
  // configuration over IPv6.
  if (ip6config_->properties().HasIPAddressAndDNS() &&
      (!network_->HasConnectionObject() || network_->IsIPv6())) {
    SetupConnection(ip6config_.get());
  }
}

void Device::ConfigureStaticIPv6Address() {
  if (!ipv6_static_properties_ || ipv6_static_properties_->address.empty()) {
    return;
  }
  IPAddress local(IPAddress::kFamilyIPv6);
  if (!local.SetAddressFromString(ipv6_static_properties_->address)) {
    LOG(ERROR) << "Local address " << ipv6_static_properties_->address
               << " is invalid";
    return;
  }
  local.set_prefix(ipv6_static_properties_->subnet_prefix);
  rtnl_handler_->AddInterfaceAddress(interface_index_, local,
                                     local.GetDefaultBroadcast(),
                                     IPAddress(IPAddress::kFamilyIPv6));
}

void Device::SetupConnection(IPConfig* ipconfig) {
  DCHECK(ipconfig);
  SLOG(this, 2) << __func__;
  network_->CreateConnection(interface_index_, link_name_, fixed_ip_params_,
                             technology_, manager_->device_info());
  if (manager_->ShouldBlackholeUserTraffic(UniqueName())) {
    ipconfig->SetBlackholedUids(manager_->GetUserTrafficUids());
  } else {
    ipconfig->ClearBlackholedUids();
  }

  network_->UpdateFromIPConfig(ipconfig->properties());
  ConfigureStaticIPv6Address();

  // Report connection type.
  Metrics::NetworkConnectionIPType ip_type =
      network_->IsIPv6() ? Metrics::kNetworkConnectionIPTypeIPv6
                         : Metrics::kNetworkConnectionIPTypeIPv4;
  metrics()->SendEnumToUMA(Metrics::kMetricNetworkConnectionIPType, technology_,
                           ip_type);

  // Report if device have IPv6 connectivity
  auto ipv6_status =
      (ip6config_ && ip6config_->properties().HasIPAddressAndDNS())
          ? Metrics::kIPv6ConnectivityStatusYes
          : Metrics::kIPv6ConnectivityStatusNo;
  metrics()->SendEnumToUMA(Metrics::kMetricIPv6ConnectivityStatus, technology_,
                           ipv6_status);

  if (selected_service_) {
    selected_service_->SetIPConfig(
        ipconfig->GetRpcIdentifier(),
        base::BindRepeating(&Device::OnStaticIPConfigChanged,
                            weak_ptr_factory_.GetWeakPtr()));

    // If the service is already in a Connected state (this happens during a
    // roam or DHCP renewal), transitioning back to Connected isn't productive.
    // Avoid this transition entirely and wait for portal detection to
    // transition us to a more informative state (either Online or some
    // portalled state). Instead, set RoamState so that clients that care about
    // the Service's state are still able to track it.
    if (!selected_service_->IsConnected()) {
      // Setting Service.State to Connected resets RoamState.
      SetServiceState(Service::kStateConnected);
    } else {
      // We set RoamState here to reflect the actual state of the Service during
      // a roam. This way, we can keep Service.State at Online or a portalled
      // state to preserve the service sort order. Note that this can be
      // triggered by a DHCP renewal that's not a result of a roam as well, but
      // it won't do anything in non-WiFi Services.
      selected_service_->SetRoamState(Service::kRoamStateConnected);
    }
    OnConnected();

    // Subtle: Start portal detection after transitioning the service
    // to the Connected state because this call may immediately transition
    // to the Online state.
    StartPortalDetection();
  }
}

void Device::OnIPConfigUpdatedFromDHCP(const IPConfig::Properties& properties,
                                       bool new_lease_acquired) {
  // |dhcp_controller_| cannot be empty when the callback is invoked.
  DCHECK(dhcp_controller_);
  DCHECK(ipconfig_);
  ipconfig_->UpdateProperties(properties);
  OnIPv4ConfigUpdated();
  if (new_lease_acquired) {
    OnGetDHCPLease();
  }
}

void Device::OnGetDHCPLease() {}
void Device::OnGetDHCPFailure() {}
void Device::OnGetSLAACAddress() {}
void Device::OnNetworkValidationStart() {}
void Device::OnNetworkValidationSuccess() {}
void Device::OnNetworkValidationFailure() {}

void Device::OnIPv4ConfigUpdated() {
  SLOG(this, 2) << __func__;
  if (selected_service_) {
    ipconfig_->ApplyStaticIPParameters(
        selected_service_->mutable_static_ip_parameters());
    if (IsUsingStaticIP() && dhcp_controller_) {
      // If we are using a statically configured IP address instead
      // of a leased IP address, release any acquired lease so it may
      // be used by others.  This allows us to merge other non-leased
      // parameters (like DNS) when they're available from a DHCP server
      // and not overridden by static parameters, but at the same time
      // we avoid taking up a dynamic IP address the DHCP server could
      // assign to someone else who might actually use it.
      dhcp_controller_->ReleaseIP(DHCPController::kReleaseReasonStaticIP);
    }
  }

  SetupConnection(ipconfig_.get());
  UpdateIPConfigsProperty();
}

void Device::OnDHCPFailure() {
  SLOG(this, 2) << __func__;

  OnGetDHCPFailure();

  // |dhcp_controller_| cannot be empty when the callback is invoked.
  DCHECK(dhcp_controller_);
  DCHECK(ipconfig_);
  if (selected_service_) {
    if (IsUsingStaticIP()) {
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
      //    parameters, overriden by any static parameters
      //    provided. We continue to use this configuration, because
      //    the only configuration element that is leased to us (IP
      //    address) will be overriden by a static parameter.
      return;
    }
  }

  ipconfig_->ResetProperties();
  UpdateIPConfigsProperty();

  // Fallback to IPv6 if possible.
  if (ip6config_ && ip6config_->properties().HasIPAddressAndDNS()) {
    if (!network_->HasConnectionObject() || !network_->IsIPv6()) {
      // Setup IPv6 connection.
      SetupConnection(ip6config_.get());
    } else {
      // Ignore IPv4 config failure, since IPv6 is up.
    }
    return;
  }

  OnIPConfigFailure();
  DestroyConnection();
}

void Device::OnStaticIPConfigChanged() {
  if (!ipconfig_ || !selected_service_) {
    LOG(ERROR) << __func__ << " called but "
               << (!ipconfig_ ? "no IPv4 config" : "no selected service");
    return;
  }

  // Clear the previously applied static IP parameters.
  ipconfig_->RestoreSavedIPParameters(
      selected_service_->mutable_static_ip_parameters());

  dispatcher()->PostTask(
      FROM_HERE, base::BindOnce(&Device::ConfigureStaticIPTask, AsWeakPtr()));

  if (dhcp_controller_) {
    // Trigger DHCP renew.
    dhcp_controller_->RenewIP();
  }
}

void Device::OnIPConfigFailure() {
  if (selected_service_) {
    Error error;
    selected_service_->DisconnectWithFailure(Service::kFailureDHCP, &error,
                                             __func__);
  }
}

void Device::OnConnected() {}

void Device::DestroyConnection() {
  SLOG(this, 2) << __func__ << " on " << link_name_;
  StopAllActivities();
  if (selected_service_) {
    selected_service_->SetIPConfig(RpcIdentifier(),
                                   /*static_ipconfig_changed_callback=*/{});
  }
  network_->DestroyConnection();
}

void Device::GetTrafficCountersCallback(
    const ServiceRefPtr& old_service,
    const ServiceRefPtr& new_service,
    const std::vector<patchpanel::TrafficCounter>& counters) {
  if (old_service) {
    old_service->RefreshTrafficCounters(counters);
  }
  if (new_service) {
    // Update the snapshot values, which will be used in future refreshes to
    // diff against the counter values. Snapshot must be initialized before
    // layer 3 configuration to ensure that we capture all traffic for the
    // service.
    new_service->InitializeTrafficCounterSnapshot(counters);
  }
}

void Device::GetTrafficCountersPatchpanelCallback(
    unsigned int id, const std::vector<patchpanel::TrafficCounter>& counters) {
  auto iter = traffic_counters_callback_map_.find(id);
  if (iter == traffic_counters_callback_map_.end() || iter->second.is_null()) {
    LOG(ERROR) << "No callback found for ID " << id;
    return;
  }
  if (counters.empty()) {
    LOG(WARNING) << "No counters found for " << link_name_;
  }
  auto callback = std::move(iter->second);
  traffic_counters_callback_map_.erase(iter);
  std::move(callback).Run(counters);
}

void Device::SelectService(const ServiceRefPtr& service) {
  SLOG(this, 2) << __func__ << ": service "
                << (service ? service->log_name() : "*reset*") << " on "
                << link_name_;

  if (selected_service_.get() == service.get()) {
    // No change to |selected_service_|. Return early to avoid
    // changing its state.
    return;
  }

  ServiceRefPtr old_service;
  if (selected_service_) {
    old_service = selected_service_;
    if (selected_service_->state() != Service::kStateFailure) {
      selected_service_->SetState(Service::kStateIdle);
    }
    selected_service_->SetIPConfig(RpcIdentifier(),
                                   /*static_ipconfig_changed_callback=*/{});
    StopAllActivities();
  }

  selected_service_ = service;
  OnSelectedServiceChanged(old_service);
  FetchTrafficCounters(old_service, selected_service_);
  adaptor_->EmitRpcIdentifierChanged(kSelectedServiceProperty,
                                     GetSelectedServiceRpcIdentifier(nullptr));
}

void Device::SetServiceState(Service::ConnectState state) {
  if (selected_service_) {
    selected_service_->SetState(state);
  }
}

void Device::SetServiceFailure(Service::ConnectFailure failure_state) {
  if (selected_service_) {
    selected_service_->SetFailure(failure_state);
  }
}

void Device::SetServiceFailureSilent(Service::ConnectFailure failure_state) {
  if (selected_service_) {
    selected_service_->SetFailureSilent(failure_state);
  }
}

bool Device::SetIPFlag(IPAddress::Family family,
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
  base::FilePath flag_file(base::StringPrintf(
      kIPFlagTemplate, ip_version.c_str(), link_name_.c_str(), flag.c_str()));
  SLOG(this, 2) << "Writing " << value << " to flag file " << flag_file.value();
  if (base::WriteFile(flag_file, value.c_str(), value.length()) != 1) {
    const auto message =
        base::StringPrintf("IP flag write failed: %s to %s", value.c_str(),
                           flag_file.value().c_str());
    if (!base::PathExists(flag_file) &&
        base::Contains(written_flags_, flag_file.value())) {
      SLOG(this, 2) << message << " (device is no longer present?)";
    } else {
      LOG(ERROR) << message;
    }
    return false;
  } else {
    written_flags_.insert(flag_file.value());
  }
  return true;
}

bool Device::RestartPortalDetection() {
  StopPortalDetection();
  return StartPortalDetection();
}

bool Device::RequestPortalDetection() {
  if (!selected_service_) {
    LOG(INFO) << link_name() << ": Skipping portal detection: no Service";
    return false;
  }

  if (!network_->HasConnectionObject()) {
    LOG(INFO) << link_name() << ": Skipping portal detection: no Connection";
    return false;
  }

  // Do not run portal detection unless in a connected state (i.e. connected,
  // online, or portalled).
  if (!selected_service_->IsConnected()) {
    LOG(INFO) << link_name()
              << ": Skipping portal detection: Service is not connected";
    return false;
  }

  if (portal_detector_.get() && portal_detector_->IsInProgress()) {
    LOG(INFO) << link_name() << ": Portal detection is already running.";
    return true;
  }

  SLOG(this, 1) << __func__ << " for: " << selected_service_->log_name();

  return StartPortalDetection();
}

// Start portal detection for |selected_service_| if enabled.
// Note: This method used to also check for a proxy configuration, however a
// proxy may or may not return a portal response depending on how it is
// configured. We run additional portal detection in Chrome if a proxy is
// configured, but still run Shill portal detection first.
bool Device::StartPortalDetection() {
  DCHECK(selected_service_);
  SLOG(this, 1) << __func__ << " for: " << selected_service_->log_name();

  if (selected_service_->IsPortalDetectionDisabled()) {
    LOG(INFO) << link_name() << ": Portal detection is disabled for service "
              << selected_service_->log_name();
    SetServiceConnectedState(Service::kStateOnline);
    return false;
  }

  // If portal detection is disabled for this technology, immediately set
  // the service state to "Online".
  if (selected_service_->IsPortalDetectionAuto() &&
      !manager_->IsPortalDetectionEnabled(technology())) {
    LOG(INFO) << link_name()
              << ": Portal detection is disabled for this technology";
    SetServiceConnectedState(Service::kStateOnline);
    return false;
  }

  if (selected_service_->HasProxyConfig()) {
    // Services with HTTP proxy configurations should not be checked by the
    // connection manager, since we don't have the ability to evaluate
    // arbitrary proxy configs and their possible credentials.
    // TODO(b/207657239) Make PortalDetector proxy-aware and compatible with
    // web proxy configurations.
    LOG(INFO) << link_name() << ": Service " << selected_service_->log_name()
              << " has proxy config; marking it online.";
    SetServiceConnectedState(Service::kStateOnline);
    return false;
  }

  portal_detector_.reset(new PortalDetector(
      dispatcher(), metrics(),
      base::Bind(&Device::PortalDetectorCallback, AsWeakPtr())));
  DCHECK(network_->HasConnectionObject());
  if (!portal_detector_->Start(manager_->GetProperties(),
                               network_->interface_name(), network_->local(),
                               network_->dns_servers())) {
    LOG(ERROR) << link_name() << ": Portal detection failed to start";
    SetServiceConnectedState(Service::kStateOnline);
    return false;
  }

  SLOG(this, 2) << link_name() << ": Portal detection has started.";
  OnNetworkValidationStart();

  return true;
}

void Device::StopPortalDetection() {
  SLOG(this, 2) << link_name() << ": Portal detection stopping.";
  portal_detector_.reset();
}

void Device::StartConnectionDiagnosticsAfterPortalDetection() {
  DCHECK(network_->HasConnectionObject());
  connection_diagnostics_.reset(new ConnectionDiagnostics(
      network_->interface_name(), network_->interface_index(),
      network_->local(), network_->gateway(), network_->dns_servers(),
      dispatcher(), metrics(), manager_->device_info(), base::DoNothing()));
  if (!connection_diagnostics_->Start(
          manager_->GetProperties().portal_http_url)) {
    connection_diagnostics_.reset();
  }
}

void Device::StopConnectionDiagnostics() {
  SLOG(this, 2) << link_name() << ": Connection diagnostics stopping.";
  connection_diagnostics_.reset();
}

void Device::EmitMACAddress(const std::string& mac_address) {
  if (mac_address.empty() ||
      MakeHardwareAddressFromString(mac_address).empty()) {
    adaptor_->EmitStringChanged(kAddressProperty, mac_address_);
  } else {
    adaptor_->EmitStringChanged(kAddressProperty, mac_address);
  }
}

void Device::set_mac_address(const std::string& mac_address) {
  mac_address_ = mac_address;
  EmitMACAddress();
}

std::optional<base::TimeDelta> Device::TimeToNextDHCPLeaseRenewal() {
  if (!dhcp_controller()) {
    return std::nullopt;
  }
  return dhcp_controller()->TimeToLeaseExpiry();
}

void Device::SetServiceConnectedState(Service::ConnectState state) {
  DCHECK(selected_service_.get());

  if (!selected_service_) {
    // A race can happen if the Service has disconnected in the meantime.
    LOG(WARNING)
        << link_name() << ": "
        << "Portal detection completed but no selected service exists.";
    return;
  }

  if (!selected_service_->IsConnected()) {
    // A race can happen if the Service is currently disconnecting.
    LOG(WARNING) << link_name() << ": "
                 << "Portal detection completed but selected service "
                 << selected_service_->log_name()
                 << " is in non-connected state.";
    return;
  }

  SLOG(this, 2) << __func__ << " Service: " << selected_service_->log_name()
                << " State: " << Service::ConnectStateToString(state);

  if (Service::IsPortalledState(state)) {
    CHECK(portal_detector_.get());
    CHECK(network_->HasConnectionObject());
    const auto next_delay = portal_detector_->GetNextAttemptDelay();
    if (!portal_detector_->Start(manager_->GetProperties(),
                                 network_->interface_name(), network_->local(),
                                 network_->dns_servers(), next_delay)) {
      LOG(ERROR) << link_name() << ": Portal detection failed to restart";
      SetServiceState(Service::kStateOnline);
      StopPortalDetection();
      return;
    }
    LOG(INFO) << link_name() << ": Portal detection retrying in " << next_delay;
    // TODO(b/216351118): this ignores the portal detection retry delay. The
    // callback should be triggered when the next attempt starts, not when it
    // is scheduled.
    OnNetworkValidationStart();
  } else {
    LOG(INFO) << link_name() << ": Portal detection finished";
    StopPortalDetection();
  }

  SetServiceState(state);
}

void Device::PortalDetectorCallback(const PortalDetector::Result& result) {
  SLOG(this, 2) << __func__ << " Device: " << link_name() << " Service: "
                << GetSelectedServiceRpcIdentifier(nullptr).value()
                << " Received status: " << result.http_status;

  int portal_status = Metrics::PortalDetectionResultToEnum(result);
  metrics()->SendEnumToUMA(Metrics::kMetricPortalResult, technology(),
                           portal_status);

  Service::ConnectState state = result.GetConnectionState();
  if (selected_service_) {
    // Set the probe URL. It should be empty if there is no redirect.
    selected_service_->SetProbeUrl(result.probe_url_string);
  }
  if (state == Service::kStateOnline) {
    SetServiceConnectedState(state);
    OnNetworkValidationSuccess();
    metrics()->SendToUMA(Metrics::kMetricPortalAttemptsToOnline, technology(),
                         result.num_attempts);
  } else {
    // Set failure phase and status.
    if (selected_service_) {
      selected_service_->SetPortalDetectionFailure(
          PortalDetector::PhaseToString(result.http_phase),
          PortalDetector::StatusToString(result.http_status),
          result.http_status_code);
    }
    SetServiceConnectedState(state);
    OnNetworkValidationFailure();
    // If portal detection was not conclusive, also start additional connection
    // diagnostics for the current network connection.
    if (state == Service::kStateNoConnectivity ||
        state == Service::kStatePortalSuspected) {
      StartConnectionDiagnosticsAfterPortalDetection();
    }
  }
}

RpcIdentifier Device::GetSelectedServiceRpcIdentifier(Error* /*error*/) {
  if (!selected_service_) {
    return RpcIdentifier("/");
  }
  return selected_service_->GetRpcIdentifier();
}

RpcIdentifiers Device::AvailableIPConfigs(Error* /*error*/) {
  RpcIdentifiers identifiers;
  if (ipconfig_) {
    identifiers.push_back(ipconfig_->GetRpcIdentifier());
  }
  if (ip6config_) {
    identifiers.push_back(ip6config_->GetRpcIdentifier());
  }
  return identifiers;
}

bool Device::IsUnderlyingDeviceEnabled() const {
  return false;
}

// callback
void Device::OnEnabledStateChanged(const ResultCallback& callback,
                                   const Error& error) {
  SLOG(this, 1) << __func__ << " (target: " << enabled_pending_ << ","
                << " success: " << error.IsSuccess() << ")"
                << " on " << link_name_;

  if (error.IsSuccess()) {
    UpdateEnabledState();
  } else {
    // Set enabled_pending_ to |enabled_| so that we don't try enabling again
    // after an error.
    enabled_pending_ = enabled_;
  }

  if (!callback.is_null())
    callback.Run(error);
}

void Device::UpdateEnabledState() {
  SLOG(this, 1) << __func__ << " (current: " << enabled_
                << ", target: " << enabled_pending_ << ")"
                << " on " << link_name_;
  enabled_ = enabled_pending_;
  if (!enabled_ && ShouldBringNetworkInterfaceDownAfterDisabled()) {
    BringNetworkInterfaceDown();
  }
  manager_->UpdateEnabledTechnologies();
  adaptor_->EmitBoolChanged(kPoweredProperty, enabled_);
}

void Device::SetEnabled(bool enable) {
  SLOG(this, 1) << __func__ << "(" << enable << ")";
  Error error;
  SetEnabledChecked(enable, false, &error, ResultCallback());

  // SetEnabledInternal might fail here if there is an unfinished enable or
  // disable operation. Don't log error in this case, as this method is only
  // called when the underlying device is already in the target state and the
  // pending operation should eventually bring the device to the expected
  // state.
  LOG_IF(ERROR, error.IsFailure() && !error.IsOngoing() &&
                    error.type() != Error::kInProgress)
      << "Enabled failed, but no way to report the failure.";
}

void Device::SetEnabledNonPersistent(bool enable,
                                     Error* error,
                                     const ResultCallback& callback) {
  SLOG(this, 1) << __func__ << "(" << enable << ")";
  SetEnabledChecked(enable, false, error, callback);
}

void Device::SetEnabledPersistent(bool enable,
                                  Error* error,
                                  const ResultCallback& callback) {
  SLOG(this, 1) << __func__ << "(" << enable << ")";
  SetEnabledChecked(enable, true, error, callback);
}

void Device::SetEnabledChecked(bool enable,
                               bool persist,
                               Error* error,
                               const ResultCallback& callback) {
  DCHECK(error);
  SLOG(this, 1) << __func__ << ": Device " << link_name_ << " "
                << (enable ? "starting" : "stopping");
  if (enable && manager_->IsTechnologyProhibited(technology())) {
    error->Populate(Error::kPermissionDenied, "The " + technology().GetName() +
                                                  " technology is prohibited");
    return;
  }

  if (enable == enabled_) {
    if (enable != enabled_pending_ && persist) {
      // Return an error, as there is an ongoing operation to achieve the
      // opposite.
      Error::PopulateAndLog(
          FROM_HERE, error, Error::kOperationFailed,
          enable ? "Cannot enable while the device is disabling."
                 : "Cannot disable while the device is enabling.");
      return;
    }
    SLOG(this, 1) << "Already in desired enable state.";
    error->Reset();
    // We can already be in the right state, but it may not be persisted.
    // Check and flush that too.
    if (persist && enabled_persistent_ != enable) {
      enabled_persistent_ = enable;
      manager_->UpdateDevice(this);
    }
    return;
  }

  if (enabled_pending_ == enable) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInProgress,
                          "Enable operation already in progress");
    return;
  }

  if (persist) {
    enabled_persistent_ = enable;
    manager_->UpdateDevice(this);
  }

  SetEnabledUnchecked(enable, error, callback);
}

void Device::SetEnabledUnchecked(bool enable,
                                 Error* error,
                                 const ResultCallback& on_enable_complete) {
  SLOG(this, 1) << __func__ << ": link: " << link_name()
                << " enable: " << enable;
  enabled_pending_ = enable;
  EnabledStateChangedCallback chained_callback = base::Bind(
      &Device::OnEnabledStateChanged, AsWeakPtr(), on_enable_complete);
  if (enable) {
    Start(error, chained_callback);
  } else {
    DestroyIPConfig();       // breaks a reference cycle
    SelectService(nullptr);  // breaks a reference cycle
    if (!ShouldBringNetworkInterfaceDownAfterDisabled()) {
      BringNetworkInterfaceDown();
    }
    SLOG(this, 3) << "Device " << link_name_ << " ipconfig_ "
                  << (ipconfig_ ? "is set." : "is not set.");
    SLOG(this, 3) << "Device " << link_name_ << " ip6config_ "
                  << (ip6config_ ? "is set." : "is not set.");
    SLOG(this, 3) << "Device " << link_name_ << " selected_service_ "
                  << (selected_service_ ? "is set." : "is not set.");
    Stop(error, chained_callback);
  }
}

void Device::UpdateIPConfigsProperty() {
  adaptor_->EmitRpcIdentifierArrayChanged(kIPConfigsProperty,
                                          AvailableIPConfigs(nullptr));
}

// static
std::vector<uint8_t> Device::MakeHardwareAddressFromString(
    const std::string& address_string) {
  std::string address_nosep;
  base::RemoveChars(address_string, ":", &address_nosep);
  std::vector<uint8_t> address_bytes;
  base::HexStringToBytes(address_nosep, &address_bytes);
  if (address_bytes.size() != kHardwareAddressLength) {
    return std::vector<uint8_t>();
  }
  return address_bytes;
}

// static
std::string Device::MakeStringFromHardwareAddress(
    const std::vector<uint8_t>& address_bytes) {
  CHECK_EQ(kHardwareAddressLength, address_bytes.size());
  return base::StringPrintf(
      "%02x:%02x:%02x:%02x:%02x:%02x", address_bytes[0], address_bytes[1],
      address_bytes[2], address_bytes[3], address_bytes[4], address_bytes[5]);
}

bool Device::RequestRoam(const std::string& addr, Error* error) {
  return false;
}

bool Device::ShouldBringNetworkInterfaceDownAfterDisabled() const {
  return false;
}

void Device::BringNetworkInterfaceDown() {
  // If |fixed_ip_params_| is true, we don't manipulate the interface state.
  if (!fixed_ip_params_)
    rtnl_handler_->SetInterfaceFlags(interface_index(), 0, IFF_UP);
}

ControlInterface* Device::control_interface() const {
  return manager_->control_interface();
}

EventDispatcher* Device::dispatcher() const {
  return manager_->dispatcher();
}

Metrics* Device::metrics() const {
  return manager_->metrics();
}

}  // namespace shill
