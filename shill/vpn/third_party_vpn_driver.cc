// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/third_party_vpn_driver.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
#include <base/containers/span.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <chromeos/dbus/service_constants.h>
#include <net-base/ip_address.h>
#include <net-base/ipv4_address.h>
#include <net-base/network_config.h>
#include <net-base/process_manager.h>

#include "shill/control_interface.h"
#include "shill/device_info.h"
#include "shill/error.h"
#include "shill/file_io.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/metrics.h"
#include "shill/store/property_accessor.h"
#include "shill/store/store_interface.h"
#include "shill/vpn/vpn_types.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kVPN;
}  // namespace Logging

namespace {

const int32_t kConstantMaxMtu = (1 << 16) - 1;
constexpr base::TimeDelta kConnectTimeout = base::Minutes(5);

std::string IPAddressFingerprint(const net_base::IPv4CIDR& cidr) {
  static const char* const hex_to_bin[] = {
      "0000", "0001", "0010", "0011", "0100", "0101", "0110", "0111",
      "1000", "1001", "1010", "1011", "1100", "1101", "1110", "1111"};
  std::string fingerprint;
  const size_t address_length = cidr.address().kAddressLength;
  const auto raw_address = cidr.address().data();
  for (size_t i = 0; i < address_length; ++i) {
    fingerprint += hex_to_bin[raw_address[i] >> 4];
    fingerprint += hex_to_bin[raw_address[i] & 0xf];
  }
  return fingerprint.substr(0, cidr.prefix_length());
}

// Returns the string value corresponding to the |key| in the given
// |parameters|. Returns std::nullopt when the |key| is missing.
// When the flag |mandatory| is set to true and the |key| is missing in
// |parameters|, an error message will be appended to |error_message|.
std::optional<std::string_view> GetParameterString(
    const std::map<std::string, std::string>& parameters,
    std::string_view key,
    bool mandatory,
    std::string* error_message) {
  if (const auto it = parameters.find(std::string(key));
      it != parameters.end()) {
    return std::make_optional<std::string_view>(it->second);
  }
  // Key not found.
  if (mandatory) {
    error_message->append(key).append(" is missing;");
  }
  return std::nullopt;
}

// Returns the int32 value corresponding to the |key| in the given |parameters|.
// If the value is a valid int32, and is between |min_value| and |max_value|,
// then it will be returned, otherwise an error message will be appended to
// |error_message|.
// When the flag |mandatory| is set to true and the |key| is missing in
// |parameters|, an error message will be appended to |error_message|.
std::optional<int32_t> GetParameterInt32(
    const std::map<std::string, std::string>& parameters,
    std::string_view key,
    int32_t min_value,
    int32_t max_value,
    bool mandatory,
    std::string* error_message) {
  const std::optional<std::string_view> value_str =
      GetParameterString(parameters, key, mandatory, error_message);
  if (!value_str.has_value()) {
    return std::nullopt;
  }
  int32_t value = 0;
  if (base::StringToInt(*value_str, &value) && value >= min_value &&
      value <= max_value) {
    return value;
  }
  // |value_str| is not a valid integer or is not in expected range.
  error_message->append(key).append(" not in expected range;");
  return std::nullopt;
}

// Returns a list of IP addresses in CIDR format corresponding to the |key| in
// the given |parameters|. The value string from the dictionary |parameters|
// will be separated by |delimiter|.
// |known_cidrs| is used to identify duplicate entries in inclusion and
// exclusion lists.
// Errors and warnings will be added to |error_message| and |warning_message|
// respectively. When the flag |mandatory| is set to true and the |key| is
// missing in |parameters|, an error will be reported in |error_message|.
std::vector<net_base::IPCIDR> GetParameterIPArrayCIDR(
    const std::map<std::string, std::string>& parameters,
    std::string_view key,
    std::string_view delimiter,
    std::set<std::string>& known_cidrs,
    bool mandatory,
    std::string* error_message,
    std::string* warning_message) {
  const std::optional<std::string_view> value_str =
      GetParameterString(parameters, key, mandatory, error_message);
  if (!value_str.has_value()) {
    return {};
  }
  const std::vector<std::string_view> cidr_str_array = base::SplitStringPiece(
      *value_str, delimiter, base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  std::vector<net_base::IPCIDR> ret;

  for (const auto& cidr_str : cidr_str_array) {
    const auto cidr = net_base::IPv4CIDR::CreateFromCIDRString(cidr_str);
    if (!cidr.has_value()) {
      warning_message->append(
          base::StrCat({cidr_str, " for ", key, " is invalid;"}));
      continue;
    }
    const std::string cidr_key = IPAddressFingerprint(*cidr);
    if (known_cidrs.contains(cidr_key)) {
      warning_message->append(base::StrCat(
          {"Duplicate entry for ", cidr_str, " in ", key, " found;"}));
      continue;
    }
    known_cidrs.insert(cidr_key);
    ret.push_back(net_base::IPCIDR(*cidr));
  }

  if (ret.empty()) {
    error_message->append(key).append(" has no valid values or is empty;");
  }

  return ret;
}

}  // namespace

const VPNDriver::Property ThirdPartyVpnDriver::kProperties[] = {
    {kProviderHostProperty, 0},
    {kProviderTypeProperty, 0},
    {kExtensionNameProperty, 0},
    {kConfigurationNameProperty, 0}};

ThirdPartyVpnDriver* ThirdPartyVpnDriver::active_client_ = nullptr;

ThirdPartyVpnDriver::ThirdPartyVpnDriver(
    Manager* manager, net_base::ProcessManager* process_manager)
    : VPNDriver(manager,
                process_manager,
                VPNType::kThirdParty,
                kProperties,
                std::size(kProperties)),
      tun_fd_(-1),
      network_config_set_(false),
      parameters_expected_(false),
      reconnect_supported_(false) {
  file_io_ = FileIO::GetInstance();
}

ThirdPartyVpnDriver::~ThirdPartyVpnDriver() {
  Cleanup();
}

void ThirdPartyVpnDriver::InitPropertyStore(PropertyStore* store) {
  VPNDriver::InitPropertyStore(store);
  store->RegisterDerivedString(
      kObjectPathSuffixProperty,
      StringAccessor(
          new CustomWriteOnlyAccessor<ThirdPartyVpnDriver, std::string>(
              this, &ThirdPartyVpnDriver::SetExtensionId,
              &ThirdPartyVpnDriver::ClearExtensionId, nullptr)));
}

bool ThirdPartyVpnDriver::Load(const StoreInterface* storage,
                               const std::string& storage_id) {
  bool return_value = VPNDriver::Load(storage, storage_id);
  if (adaptor_interface_ == nullptr) {
    storage->GetString(storage_id, kObjectPathSuffixProperty,
                       &object_path_suffix_);
    adaptor_interface_ = control_interface()->CreateThirdPartyVpnAdaptor(this);
  }
  return return_value;
}

bool ThirdPartyVpnDriver::Save(StoreInterface* storage,
                               const std::string& storage_id,
                               bool save_credentials) {
  bool return_value = VPNDriver::Save(storage, storage_id, save_credentials);
  storage->SetString(storage_id, kObjectPathSuffixProperty,
                     object_path_suffix_);
  return return_value;
}

void ThirdPartyVpnDriver::ClearExtensionId(Error* error) {
  error->Populate(Error::kIllegalOperation,
                  "Clearing extension id is not allowed.");
}

bool ThirdPartyVpnDriver::SetExtensionId(const std::string& value,
                                         Error* error) {
  if (adaptor_interface_ == nullptr) {
    object_path_suffix_ = value;
    adaptor_interface_ = control_interface()->CreateThirdPartyVpnAdaptor(this);
    return true;
  }
  error->Populate(Error::kAlreadyExists, "Extension ID is set");
  return false;
}

void ThirdPartyVpnDriver::UpdateConnectionState(
    Service::ConnectState connection_state, std::string* error_message) {
  if (active_client_ != this) {
    error_message->append("Unexpected call");
    return;
  }
  if (event_handler_ && connection_state == Service::kStateFailure) {
    FailService(Service::kFailureConnect, "Failure state set by D-Bus caller");
    return;
  }
  if (!event_handler_ || connection_state != Service::kStateOnline) {
    // We expect "failure" and "connected" messages from the client, but we
    // only set state for these "failure" messages. "connected" message (which
    // is corresponding to kStateOnline here) will simply be ignored.
    error_message->append("Invalid argument");
  }
}

void ThirdPartyVpnDriver::SendPacket(const std::vector<uint8_t>& ip_packet,
                                     std::string* error_message) {
  if (active_client_ != this) {
    error_message->append("Unexpected call");
    return;
  } else if (tun_fd_ < 0) {
    error_message->append("Device not open");
    return;
  } else if (file_io_->Write(tun_fd_, ip_packet.data(), ip_packet.size()) !=
             static_cast<ssize_t>(ip_packet.size())) {
    error_message->append("Partial write");
    adaptor_interface_->EmitPlatformMessage(
        static_cast<uint32_t>(PlatformMessage::kError));
  }
}

void ThirdPartyVpnDriver::SetParameters(
    const std::map<std::string, std::string>& parameters,
    std::string* error_message,
    std::string* warning_message) {
  // TODO(kaliamoorthi): Add IPV6 support.
  if (!parameters_expected_ || active_client_ != this) {
    error_message->append("Unexpected call");
    return;
  }

  network_config_ = std::make_optional<net_base::NetworkConfig>();

  const std::optional<std::string_view> address = GetParameterString(
      parameters, kAddressParameterThirdPartyVpn, true, error_message);
  const std::optional<int32_t> subnet_prefix =
      GetParameterInt32(parameters, kSubnetPrefixParameterThirdPartyVpn, 0, 32,
                        true, error_message);
  if (address.has_value() && subnet_prefix.has_value()) {
    network_config_->ipv4_address =
        net_base::IPv4CIDR::CreateFromStringAndPrefix(*address, *subnet_prefix);
    if (!network_config_->ipv4_address.has_value()) {
      error_message->append(kAddressParameterThirdPartyVpn)
          .append(" is not a valid IP;");
    } else {
      network_config_->ipv4_gateway = network_config_->ipv4_address->address();
    }
  }

  const std::optional<std::string_view> broadcast_address =
      GetParameterString(parameters, kBroadcastAddressParameterThirdPartyVpn,
                         false, error_message);
  if (broadcast_address.has_value()) {
    network_config_->ipv4_broadcast =
        net_base::IPv4Address::CreateFromString(*broadcast_address);
    if (!network_config_->ipv4_broadcast.has_value()) {
      error_message->append(kBroadcastAddressParameterThirdPartyVpn)
          .append(" is not a valid IP;");
    }
  }

  network_config_->mtu =
      GetParameterInt32(parameters, kMtuParameterThirdPartyVpn,
                        net_base::NetworkConfig::kMinIPv4MTU, kConstantMaxMtu,
                        false, error_message);

  const std::string non_ip_delimiter{kNonIPDelimiter};
  const std::string ip_delimiter{kIPDelimiter};

  const std::optional<std::string_view> dns_search_domains_str =
      GetParameterString(parameters, kDomainSearchParameterThirdPartyVpn, false,
                         error_message);
  if (dns_search_domains_str.has_value()) {
    const std::vector<std::string_view> dns_search_domains_array =
        base::SplitStringPiece(*dns_search_domains_str, non_ip_delimiter,
                               base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
    if (dns_search_domains_array.empty()) {
      error_message->append(kDomainSearchParameterThirdPartyVpn)
          .append(" has no valid values or is empty;");
    } else {
      // Deduplicate search domains
      base::flat_set<std::string_view> dns_search_domains_dedup;
      for (const auto& domain : dns_search_domains_array) {
        if (!dns_search_domains_dedup.contains(domain)) {
          network_config_->dns_search_domains.push_back(std::string(domain));
          dns_search_domains_dedup.insert(domain);
        }
      }
    }
  }

  const std::optional<std::string_view> dns_servers_str = GetParameterString(
      parameters, kDnsServersParameterThirdPartyVpn, false, error_message);
  if (dns_servers_str.has_value()) {
    const std::vector<std::string_view> dns_servers_array =
        base::SplitStringPiece(*dns_servers_str, ip_delimiter,
                               base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
    for (const auto& dns_server : dns_servers_array) {
      auto dns = net_base::IPAddress::CreateFromString(dns_server);
      if (dns.has_value()) {
        network_config_->dns_servers.push_back(*dns);
      } else {
        warning_message->append(
            base::StrCat({dns_server, " for ",
                          kDnsServersParameterThirdPartyVpn, " is invalid;"}));
      }
    }
  }

  // Used to identify duplicate entries in inclusion and exclusion lists.
  std::set<std::string> known_cidrs;

  network_config_->excluded_route_prefixes = GetParameterIPArrayCIDR(
      parameters, kExclusionListParameterThirdPartyVpn, ip_delimiter,
      known_cidrs, true, error_message, warning_message);
  if (!network_config_->excluded_route_prefixes.empty()) {
    // The first excluded IP is used to find the default gateway. The logic that
    // finds the default gateway does not work for default route "0.0.0.0/0".
    // Hence, this code ensures that the first IP is not default.
    const net_base::IPCIDR& cidr = network_config_->excluded_route_prefixes[0];
    if (cidr.IsDefault()) {
      if (network_config_->excluded_route_prefixes.size() > 1) {
        std::swap(network_config_->excluded_route_prefixes[0],
                  network_config_->excluded_route_prefixes[1]);
      } else {
        // When there is only a single entry which is a default address, it can
        // be cleared since the default behavior is to not route any traffic to
        // the tunnel interface.
        network_config_->excluded_route_prefixes.clear();
      }
    }
  }

  reconnect_supported_ = false;
  const std::optional<std::string_view> reconnect_supported_str =
      GetParameterString(parameters, kReconnectParameterThirdPartyVpn, false,
                         error_message);
  if (reconnect_supported_str.has_value()) {
    if (reconnect_supported_str == "true") {
      reconnect_supported_ = true;
    } else if (reconnect_supported_str != "false") {
      error_message->append(kReconnectParameterThirdPartyVpn)
          .append(" not a valid boolean;");
    }
  }

  network_config_->included_route_prefixes = GetParameterIPArrayCIDR(
      parameters, kInclusionListParameterThirdPartyVpn, ip_delimiter,
      known_cidrs, true, error_message, warning_message);

  if (!error_message->empty()) {
    LOG(ERROR) << __func__ << ": " << error_message;
    return;
  }
  network_config_->ipv4_default_route = false;
  network_config_->ipv6_blackhole_route = true;
  if (!network_config_set_) {
    network_config_set_ = true;
    metrics()->SendEnumToUMA(Metrics::kMetricVpnDriver,
                             Metrics::kVpnDriverThirdParty);
  }

  if (event_handler_) {
    event_handler_->OnDriverConnected(interface_name_, interface_index_);
  } else {
    LOG(ERROR) << "Missing service callback";
  }
}

void ThirdPartyVpnDriver::OnTunReadable() {
  uint8_t buf[4096];
  const ssize_t len = file_io_->Read(tun_fd_, buf, sizeof(buf));
  if (len < 0) {
    PLOG(ERROR) << "Failed to read tun fd";
    CHECK_EQ(active_client_, this);
    adaptor_interface_->EmitPlatformMessage(
        static_cast<uint32_t>(PlatformMessage::kError));
  } else {
    OnInput({buf, static_cast<size_t>(len)});
  }
}

void ThirdPartyVpnDriver::OnInput(base::span<const uint8_t> data) {
  // Not all Chrome apps can properly handle being passed IPv6 packets. This
  // usually should not be an issue because we prevent IPv6 traffic from being
  // routed to this VPN. However, the kernel itself can sometimes send IPv6
  // packets to an interface--even before we set up our routing
  // rules. Therefore, we drop non-IPv4 traffic here.
  //
  // See from RFC 791 Section 3.1 that the high nibble of the first byte in an
  // IP header represents the IP version (4 in this case).
  if ((data[0] & 0xf0) != 0x40) {
    SLOG(1) << "Dropping non-IPv4 packet";
    return;
  }

  // TODO(kaliamoorthi): This is not efficient, transfer the descriptor over to
  // chrome browser or use a pipe in between. Avoid using DBUS for packet
  // transfer.
  std::vector<uint8_t> ip_packet(std::begin(data), std::end(data));
  adaptor_interface_->EmitPacketReceived(ip_packet);
}

void ThirdPartyVpnDriver::Cleanup() {
  tun_watcher_.reset();
  if (tun_fd_ > 0) {
    file_io_->Close(tun_fd_);
    tun_fd_ = -1;
  }
  if (active_client_ == this) {
    adaptor_interface_->EmitPlatformMessage(
        static_cast<uint32_t>(PlatformMessage::kDisconnected));
    active_client_ = nullptr;
  }
  parameters_expected_ = false;
  reconnect_supported_ = false;

  if (!interface_name_.empty()) {
    manager()->device_info()->DeleteInterface(interface_index_);
    interface_name_.clear();
    interface_index_ = -1;
  }
}

base::TimeDelta ThirdPartyVpnDriver::ConnectAsync(EventHandler* handler) {
  SLOG(2) << __func__;
  event_handler_ = handler;
  if (!manager()->device_info()->CreateTunnelInterface(base::BindOnce(
          &ThirdPartyVpnDriver::OnLinkReady, weak_factory_.GetWeakPtr()))) {
    dispatcher()->PostTask(
        FROM_HERE,
        base::BindOnce(&ThirdPartyVpnDriver::FailService,
                       weak_factory_.GetWeakPtr(), Service::kFailureInternal,
                       "Could not to create tunnel interface."));
    return kTimeoutNone;
  }
  return kConnectTimeout;
}

void ThirdPartyVpnDriver::OnLinkReady(const std::string& link_name,
                                      int interface_index) {
  SLOG(2) << __func__;
  if (!event_handler_) {
    LOG(ERROR) << "event_handler_ is not set";
    return;
  }

  CHECK(adaptor_interface_);
  CHECK(!active_client_);

  interface_name_ = link_name;
  interface_index_ = interface_index;

  network_config_ = std::make_optional<net_base::NetworkConfig>();
  network_config_set_ = false;

  tun_fd_ = manager()->device_info()->OpenTunnelInterface(interface_name_);
  if (tun_fd_ < 0) {
    FailService(Service::kFailureInternal, "Unable to open tun interface");
    return;
  }

  tun_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      tun_fd_, base::BindRepeating(&ThirdPartyVpnDriver::OnTunReadable,
                                   base::Unretained(this)));
  if (tun_watcher_ == nullptr) {
    LOG(ERROR) << "Failed on watching tun fd";
    return;
  }

  active_client_ = this;
  parameters_expected_ = true;
  adaptor_interface_->EmitPlatformMessage(
      static_cast<uint32_t>(PlatformMessage::kConnected));
}

std::unique_ptr<net_base::NetworkConfig> ThirdPartyVpnDriver::GetNetworkConfig()
    const {
  if (!network_config_.has_value()) {
    LOG(DFATAL) << "network_config_ is invalid.";
    return nullptr;
  }
  return std::make_unique<net_base::NetworkConfig>(*network_config_);
}

void ThirdPartyVpnDriver::FailService(Service::ConnectFailure failure,
                                      std::string_view error_details) {
  SLOG(2) << __func__ << "(" << error_details << ")";
  Cleanup();
  if (event_handler_) {
    event_handler_->OnDriverFailure(failure, error_details);
    event_handler_ = nullptr;
  }
}

void ThirdPartyVpnDriver::Disconnect() {
  SLOG(2) << __func__;
  CHECK(adaptor_interface_);
  if (active_client_ == this) {
    Cleanup();
  }
  event_handler_ = nullptr;
}

void ThirdPartyVpnDriver::OnDefaultPhysicalServiceEvent(
    DefaultPhysicalServiceEvent event) {
  if (!event_handler_)
    return;

  if (event == kDefaultPhysicalServiceDown ||
      event == kDefaultPhysicalServiceChanged) {
    if (!reconnect_supported_) {
      FailService(Service::kFailureInternal,
                  "Underlying network disconnected.");
      return;
    }
  }

  PlatformMessage message;
  switch (event) {
    case kDefaultPhysicalServiceUp:
      message = PlatformMessage::kLinkUp;
      event_handler_->OnDriverReconnecting(kConnectTimeout);
      break;
    case kDefaultPhysicalServiceDown:
      message = PlatformMessage::kLinkDown;
      event_handler_->OnDriverReconnecting(kTimeoutNone);
      break;
    case kDefaultPhysicalServiceChanged:
      message = PlatformMessage::kLinkChanged;
      event_handler_->OnDriverReconnecting(kConnectTimeout);
      break;
    default:
      NOTREACHED();
  }

  adaptor_interface_->EmitPlatformMessage(static_cast<uint32_t>(message));
}

void ThirdPartyVpnDriver::OnBeforeSuspend(ResultCallback callback) {
  if (event_handler_ && reconnect_supported_) {
    // FIXME: Currently the VPN app receives this message at the same time
    // as the resume message, even if shill adds a delay to hold off the
    // suspend sequence.
    adaptor_interface_->EmitPlatformMessage(
        static_cast<uint32_t>(PlatformMessage::kSuspend));
  }
  std::move(callback).Run(Error(Error::kSuccess));
}

void ThirdPartyVpnDriver::OnAfterResume() {
  if (event_handler_ && reconnect_supported_) {
    // Transition back to Configuring state so that the app can perform
    // DNS lookups and reconnect.
    event_handler_->OnDriverReconnecting(kConnectTimeout);
    adaptor_interface_->EmitPlatformMessage(
        static_cast<uint32_t>(PlatformMessage::kResume));
  }
}

void ThirdPartyVpnDriver::OnConnectTimeout() {
  SLOG(2) << __func__;
  if (!event_handler_) {
    LOG(DFATAL) << "event_handler_ is not set";
    return;
  }
  adaptor_interface_->EmitPlatformMessage(
      static_cast<uint32_t>(PlatformMessage::kError));
  FailService(Service::kFailureConnect, "Connection timed out");
}

}  // namespace shill
