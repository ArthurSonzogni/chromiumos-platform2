// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/openvpn_driver.h"

#include <arpa/inet.h>

#include <algorithm>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/containers/fixed_flat_map.h>
#include <base/containers/flat_map.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <chromeos/dbus/service_constants.h>
#include <net-base/ip_address.h>
#include <net-base/ip_address_utils.h>
#include <net-base/ipv4_address.h>
#include <net-base/ipv6_address.h>
#include <net-base/network_config.h>
#include <net-base/process_manager.h>

#include "shill/certificate_file.h"
#include "shill/device_info.h"
#include "shill/error.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/rpc_task.h"
#include "shill/vpn/openvpn_management_server.h"
#include "shill/vpn/vpn_service.h"
#include "shill/vpn/vpn_types.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kVPN;
}  // namespace Logging

namespace {

constexpr char kChromeOSReleaseName[] = "CHROMEOS_RELEASE_NAME";
constexpr char kChromeOSReleaseVersion[] = "CHROMEOS_RELEASE_VERSION";
constexpr char kOpenVPNForeignOptionPrefix[] = "foreign_option_";
constexpr char kOpenVPNIfconfigLocal[] = "ifconfig_local";
constexpr char kOpenVPNIfconfigNetmask[] = "ifconfig_netmask";
constexpr char kOpenVPNIfconfigRemote[] = "ifconfig_remote";
constexpr char kOpenVPNIfconfigIPv6Local[] = "ifconfig_ipv6_local";
constexpr char kOpenVPNIfconfigIPv6Netbits[] = "ifconfig_ipv6_netbits";
constexpr char kOpenVPNRedirectGateway[] = "redirect_gateway";
constexpr char kOpenVPNTunMTU[] = "tun_mtu";

// Typically OpenVPN will set environment variables for IPv4 like:
//   route_net_gateway=<existing default LAN gateway>
//   route_vpn_gateway=10.8.0.1
//   route_gateway_1=10.8.0.1
//   route_netmask_1=255.255.255.0
//   route_network_1=192.168.10.0
// This example shows a split include route of 192.168.10.0/24, and
// 10.8.0.1 is the ifconfig_remote (remote peer) address.
//
// For IPv6, they will be like:
//   ifconfig_ipv6_local: fdfd::1000
//   ifconfig_ipv6_netbits: 64
//   ifconfig_ipv6_remote: fdfd::1
//   route_ipv6_gateway_1: fdfd::1
//   route_ipv6_network_1: ::/3
// Different from IPv4, for a route entry, there are only two variables for it
// in IPv6, and the network variable will be a prefix string.

constexpr std::string_view kOpenVPNRouteOptionPrefix = "route_";
constexpr std::string_view kOpenVPNRouteIPv6OptionPrefix = "route_ipv6_";
constexpr char kOpenVPNRouteNetGateway[] = "route_net_gateway";
constexpr char kOpenVPNRouteVPNGateway[] = "route_vpn_gateway";
constexpr char kOpenVPNRouteNetworkPrefix[] = "network_";
constexpr char kOpenVPNRouteNetmaskPrefix[] = "netmask_";
constexpr char kOpenVPNRouteGatewayPrefix[] = "gateway_";

constexpr char kDefaultPKCS11Provider[] = "libchaps.so";

// Some configurations pass the netmask in the ifconfig_remote property.
// This is due to some servers not explicitly indicating that they are using
// a "broadcast mode" network instead of peer-to-peer.  See
// http://crbug.com/241264 for an example of this issue.
constexpr char kSuspectedNetmaskPrefix[] = "255.";

constexpr char kOpenVPNPath[] = "/usr/sbin/openvpn";
constexpr char kOpenVPNScript[] = SHIMDIR "/openvpn-script";

// Directory where OpenVPN configuration files are exported while the
// process is running.
constexpr char kDefaultOpenVPNConfigurationDirectory[] =
    RUNDIR "/openvpn_config";

}  // namespace

// static
const VPNDriver::Property OpenVPNDriver::kProperties[] = {
    {kOpenVPNAuthNoCacheProperty, 0},
    {kOpenVPNAuthProperty, 0},
    {kOpenVPNAuthRetryProperty, 0},
    {kOpenVPNAuthUserPassProperty, 0},
    {kOpenVPNCipherProperty, 0},
    {kOpenVPNClientCertIdProperty, Property::kCredential},
    {kOpenVPNCompLZOProperty, 0},
    {kOpenVPNCompNoAdaptProperty, 0},
    {kOpenVPNCompressProperty, 0},
    {kOpenVPNExtraHostsProperty, Property::kArray},
    {kOpenVPNIgnoreDefaultRouteProperty, 0},
    {kOpenVPNKeyDirectionProperty, 0},
    {kOpenVPNNsCertTypeProperty, 0},
    {kOpenVPNOTPProperty,
     Property::kEphemeral | Property::kCredential | Property::kWriteOnly},
    {kOpenVPNPasswordProperty, Property::kCredential | Property::kWriteOnly},
    {kOpenVPNPinProperty, Property::kCredential},
    {kOpenVPNPortProperty, 0},
    {kOpenVPNProtoProperty, 0},
    {kOpenVPNPushPeerInfoProperty, 0},
    {kOpenVPNRemoteCertEKUProperty, 0},
    {kOpenVPNRemoteCertKUProperty, 0},
    {kOpenVPNRemoteCertTLSProperty, 0},
    {kOpenVPNRenegSecProperty, 0},
    {kOpenVPNServerPollTimeoutProperty, 0},
    {kOpenVPNShaperProperty, 0},
    {kOpenVPNStaticChallengeProperty, 0},
    {kOpenVPNTLSAuthContentsProperty, 0},
    {kOpenVPNTLSRemoteProperty, 0},
    {kOpenVPNTLSVersionMinProperty, 0},
    {kOpenVPNTokenProperty,
     Property::kEphemeral | Property::kCredential | Property::kWriteOnly},
    {kOpenVPNUserProperty, 0},
    {kProviderHostProperty, 0},
    {kProviderTypeProperty, 0},
    {kOpenVPNCaCertPemProperty, Property::kArray},
    {kOpenVPNExtraCertPemProperty, Property::kArray},
    {kOpenVPNPingExitProperty, 0},
    {kOpenVPNPingProperty, 0},
    {kOpenVPNPingRestartProperty, 0},
    {kOpenVPNTLSAuthProperty, 0},
    {kOpenVPNVerbProperty, 0},
    {kOpenVPNVerifyHashProperty, 0},
    {kOpenVPNVerifyX509NameProperty, 0},
    {kOpenVPNVerifyX509TypeProperty, 0},
    {kVPNMTUProperty, 0},
};

OpenVPNDriver::OpenVPNDriver(Manager* manager,
                             net_base::ProcessManager* process_manager)
    : VPNDriver(manager,
                process_manager,
                VPNType::kOpenVPN,
                kProperties,
                std::size(kProperties)),
      management_server_(new OpenVPNManagementServer(this)),
      certificate_file_(new CertificateFile()),
      extra_certificates_file_(new CertificateFile()),
      lsb_release_file_(kLSBReleaseFile),
      openvpn_config_directory_(kDefaultOpenVPNConfigurationDirectory),
      pid_(0),
      vpn_util_(VPNUtil::New()) {}

OpenVPNDriver::~OpenVPNDriver() {
  Cleanup();
}

void OpenVPNDriver::FailService(Service::ConnectFailure failure,
                                std::string_view error_details) {
  SLOG(2) << __func__ << "(" << error_details << ")";
  Cleanup();
  if (event_handler_) {
    event_handler_->OnDriverFailure(failure, error_details);
    event_handler_ = nullptr;
  }
}

void OpenVPNDriver::Cleanup() {
  // Disconnecting the management interface will terminate the openvpn
  // process. Ensure this is handled robustly by first unregistering
  // the callback for OnOpenVPNDied, and then terminating and reaping
  // the process with StopProcess().
  if (pid_) {
    process_manager()->UpdateExitCallback(pid_, base::DoNothing());
  }
  management_server_->Stop();
  if (!tls_auth_file_.empty()) {
    base::DeleteFile(tls_auth_file_);
    tls_auth_file_.clear();
  }
  if (!openvpn_config_file_.empty()) {
    base::DeleteFile(openvpn_config_file_);
    openvpn_config_file_.clear();
  }
  rpc_task_.reset();
  params_.clear();
  network_config_ = std::nullopt;
  if (pid_) {
    process_manager()->StopProcessAndBlock(pid_);
    pid_ = 0;
  }

  if (!interface_name_.empty()) {
    manager()->device_info()->DeleteInterface(interface_index_);
    interface_name_.clear();
    interface_index_ = -1;
  }
}

// static
std::string OpenVPNDriver::JoinOptions(
    const std::vector<std::vector<std::string>>& options, char separator) {
  std::vector<std::string> option_strings;
  for (const auto& option : options) {
    std::vector<std::string> quoted_option;
    for (const auto& argument : option) {
      if (argument.find(' ') != std::string::npos ||
          argument.find('\t') != std::string::npos ||
          argument.find('"') != std::string::npos ||
          argument.find(separator) != std::string::npos) {
        std::string quoted_argument(argument);
        const char separator_chars[] = {separator, '\0'};
        base::ReplaceChars(argument, separator_chars, " ", &quoted_argument);
        base::ReplaceChars(quoted_argument, "\\", "\\\\", &quoted_argument);
        base::ReplaceChars(quoted_argument, "\"", "\\\"", &quoted_argument);
        quoted_option.push_back("\"" + quoted_argument + "\"");
      } else {
        quoted_option.push_back(argument);
      }
    }
    option_strings.push_back(base::JoinString(quoted_option, " "));
  }
  return base::JoinString(option_strings, std::string{separator});
}

bool OpenVPNDriver::WriteConfigFile(
    const std::vector<std::vector<std::string>>& options,
    base::FilePath* config_file) {
  if (!vpn_util_->PrepareConfigDirectory(openvpn_config_directory_)) {
    LOG(ERROR) << "Unable to setup OpenVPN config directory.";
    return false;
  }

  std::string contents = JoinOptions(options, '\n');
  contents.push_back('\n');
  if (!base::CreateTemporaryFileInDir(openvpn_config_directory_, config_file) ||
      !vpn_util_->WriteConfigFile(*config_file, contents)) {
    LOG(ERROR) << "Unable to setup OpenVPN config file.";
    return false;
  }

  return true;
}

bool OpenVPNDriver::SpawnOpenVPN() {
  SLOG(2) << __func__ << "(" << interface_name_ << ")";

  std::vector<std::vector<std::string>> options;
  Error error;
  pid_t openvpn_pid;
  InitOptions(&options, &error);
  if (error.IsFailure()) {
    return false;
  }
  LOG(INFO) << "OpenVPN process options: " << JoinOptions(options, ',');
  if (!WriteConfigFile(options, &openvpn_config_file_)) {
    return false;
  }

  // TODO(quiche): This should be migrated to use ExternalTask.
  // (crbug.com/246263).
  CHECK(!pid_);

  const std::vector<std::string> args = GetCommandLineArgs();
  LOG(INFO) << "OpenVPN command line args: " << base::JoinString(args, " ");

  // OpenSSL compatibility settings.
  // TODO(crbug.com/1047146): Drop these stop-gaps after addressing the
  // underlying problems described in the bug.
  const std::map<std::string, std::string> kEnv = {
      {"OPENSSL_CONF", "/etc/ssl/openssl.cnf.compat"},
      {"OPENSSL_CHROMIUM_SKIP_TRUSTED_PURPOSE_CHECK", "1"},
      {"OPENSSL_CHROMIUM_GENERATE_METRICS", "1"},
  };

  net_base::ProcessManager::MinijailOptions minijail_options;
  minijail_options.user = "vpn";
  minijail_options.group = "vpn";
  // openvpn needs CAP_NET_ADMIN for several operations, e.g, set SO_MARK on the
  // socket and set tx queue length.
  minijail_options.capmask = CAP_TO_MASK(CAP_NET_ADMIN);
  minijail_options.inherit_supplementary_groups = true;
  openvpn_pid = process_manager()->StartProcessInMinijail(
      FROM_HERE, base::FilePath(kOpenVPNPath), args, kEnv, minijail_options,
      base::BindOnce(&OpenVPNDriver::OnOpenVPNDied, base::Unretained(this)));
  if (openvpn_pid == -1) {
    LOG(ERROR) << "Minijail couldn't run our child process";
    return false;
  }

  pid_ = openvpn_pid;
  return true;
}

void OpenVPNDriver::OnOpenVPNDied(int exit_status) {
  SLOG(2) << __func__ << "(" << pid_ << ", " << exit_status << ")";
  pid_ = 0;
  FailService(Service::kFailureInternal, Service::kErrorDetailsNone);
  // TODO(petkov): Figure if we need to restart the connection.
}

void OpenVPNDriver::GetLogin(std::string* /*user*/, std::string* /*password*/) {
  NOTREACHED();
}

void OpenVPNDriver::Notify(const std::string& reason,
                           const std::map<std::string, std::string>& dict) {
  LOG(INFO) << "IP configuration received: " << reason;
  // We only registered "--up" script so this should be the only
  // reason we get notified here. Note that "--up-restart" is set
  // so we will get notification also upon reconnection.
  if (reason != "up") {
    LOG(DFATAL) << "Unexpected notification reason";
    return;
  }
  // On restart/reconnect, update the existing dict, and generate IP
  // configurations from it.
  for (const auto& [k, v] : dict) {
    params_[k] = v;
  }
  network_config_ = ParseNetworkConfig(
      params_,
      const_args()->Contains<std::string>(kOpenVPNIgnoreDefaultRouteProperty));
  if (!network_config_.has_value()) {
    FailService(Service::kFailureConnect, "No valid IP config");
    return;
  }
  ReportConnectionMetrics();
  if (event_handler_) {
    event_handler_->OnDriverConnected(interface_name_, interface_index_);
  } else {
    LOG(DFATAL) << "Missing service callback";
  }
}

std::unique_ptr<net_base::NetworkConfig> OpenVPNDriver::GetNetworkConfig()
    const {
  if (!network_config_.has_value()) {
    return nullptr;
  }
  return std::make_unique<net_base::NetworkConfig>(*network_config_);
}

// static
std::optional<net_base::NetworkConfig> OpenVPNDriver::ParseNetworkConfig(
    const std::map<std::string, std::string>& configuration,
    bool ignore_redirect_gateway) {
  // Values parsed from |configuration|.
  ForeignOptions foreign_options;
  int mtu = 0;
  std::optional<net_base::IPv4Address> ipv4_local;
  std::optional<int> ipv4_prefix = std::nullopt;
  std::optional<net_base::IPv4Address> ipv4_remote;
  bool ipv4_redirect_gateway = false;
  std::optional<net_base::IPv6Address> ipv6_local;
  std::optional<int> ipv6_prefix = std::nullopt;

  for (const auto& [key, value] : configuration) {
    SLOG(2) << "Processing: " << key << " -> " << value;
    if (base::EqualsCaseInsensitiveASCII(key, kOpenVPNIfconfigLocal)) {
      ipv4_local = net_base::IPv4Address::CreateFromString(value);
      if (!ipv4_local.has_value()) {
        LOG(WARNING) << "Failed to parse IPv4 local address from " << value;
      }
    } else if (base::EqualsCaseInsensitiveASCII(key, kOpenVPNIfconfigNetmask)) {
      const auto netmask = net_base::IPv4Address::CreateFromString(value);
      if (netmask) {
        ipv4_prefix = net_base::IPv4CIDR::GetPrefixLength(*netmask);
      }
      if (!ipv4_prefix) {
        LOG(WARNING) << "Failed to get prefix length from " << value;
      }
    } else if (base::EqualsCaseInsensitiveASCII(key, kOpenVPNIfconfigRemote)) {
      ipv4_remote = net_base::IPv4Address::CreateFromString(value);
      if (!ipv4_remote.has_value()) {
        LOG(WARNING) << "Failed to parse IPv4 remote address from " << value;
      } else if (base::StartsWith(value, kSuspectedNetmaskPrefix,
                                  base::CompareCase::INSENSITIVE_ASCII)) {
        LOG(WARNING) << "Option " << key << " value " << value
                     << " looks more like a netmask than a peer address; "
                     << "assuming it is the former.";
        // In this situation, we unset |ipv4_remote|.
        // NetworkApplier::ApplyRoute() will treat the interface as if it
        // were a broadcast-style network. The kernel will, automatically set
        // the peer address equal to the local address.
        ipv4_prefix = net_base::IPv4CIDR::GetPrefixLength(*ipv4_remote);
        if (ipv4_prefix) {
          ipv4_remote = std::nullopt;
        } else {
          LOG(WARNING) << "Failed to get prefix length from " << value;
        }
      }
    } else if (base::EqualsCaseInsensitiveASCII(key, kOpenVPNRedirectGateway)) {
      if (ignore_redirect_gateway) {
        LOG(INFO) << "Ignoring default route parameter as requested by "
                  << "configuration.";
      } else {
        ipv4_redirect_gateway = true;
      }
    } else if (base::EqualsCaseInsensitiveASCII(key,
                                                kOpenVPNIfconfigIPv6Local)) {
      ipv6_local = net_base::IPv6Address::CreateFromString(value);
      if (!ipv6_local.has_value()) {
        LOG(WARNING) << "Failed to parse IPv6 local address from " << value;
      }
    } else if (base::EqualsCaseInsensitiveASCII(key,
                                                kOpenVPNIfconfigIPv6Netbits)) {
      int prefix = 0;
      if (!base::StringToInt(value, &prefix) ||
          !net_base::IPv6CIDR::IsValidPrefixLength(prefix)) {
        LOG(ERROR) << "IPv6 netbits ignored, value=" << value;
      } else {
        ipv6_prefix = prefix;
      }
    } else if (base::EqualsCaseInsensitiveASCII(key, kOpenVPNTunMTU)) {
      if (!base::StringToInt(value, &mtu)) {
        LOG(ERROR) << "Failed to parse MTU " << value;
      }
    } else if (base::StartsWith(key, kOpenVPNForeignOptionPrefix,
                                base::CompareCase::INSENSITIVE_ASCII)) {
      const auto suffix = key.substr(strlen(kOpenVPNForeignOptionPrefix));
      int order = 0;
      if (base::StringToInt(suffix, &order)) {
        foreign_options[order] = value;
      } else {
        LOG(ERROR) << "Ignored unexpected foreign option suffix: " << suffix;
      }
    } else if (base::StartsWith(key, kOpenVPNRouteOptionPrefix,
                                base::CompareCase::INSENSITIVE_ASCII)) {
      // These options will be parsed later in |ParseIPv4RouteOptions| and
      // |ParseIPv6RouteOptions|.
    } else {
      SLOG(2) << "Key ignored.";
    }
  }

  const bool has_ipv4 = ipv4_local.has_value();
  const bool has_ipv6 = ipv6_local.has_value();
  if (!has_ipv4 && !has_ipv6) {
    return std::nullopt;
  }

  auto network_config = std::make_optional<net_base::NetworkConfig>();

  std::vector<std::string> search_domains;
  std::vector<net_base::IPAddress> dns_servers;
  if (!foreign_options.empty()) {
    ParseForeignOptions(foreign_options, &network_config->dns_search_domains,
                        &network_config->dns_servers);
  } else {
    LOG(INFO) << "No foreign option provided";
  }

  if (mtu != 0) {
    const int min_mtu = has_ipv6 ? net_base::NetworkConfig::kMinIPv6MTU
                                 : net_base::NetworkConfig::kMinIPv4MTU;
    if (mtu < min_mtu) {
      LOG(ERROR) << "MTU value " << mtu << " ignored";
    } else {
      network_config->mtu = mtu;
    }
  }

  // Notes on `redirect-gateway`:
  //
  // In openvpn configuration, the user can add a `ipv6` flag to the
  // `redirect-gateway` option to indicate a default route for IPv6, but in the
  // context of environment variables passed from openvpn, `redirect-gateway` is
  // an IPv4-only option: for IPv6, openvpn client will translate it into routes
  // and set them in the variables. So at the server side, suppose there is no
  // route configured directly, there are 4 cases:
  // - No `redirect-gateway`: indicates no default route for both v4 and v6;
  //   openvpn client will set neither `redirect-gateway` nor routes in env
  //   variables.
  // - `redirect-gateway (def1)?`: indicates IPv4-only default route; openvpn
  //   client will set only `redirect-gateway` but no route in env variables.
  // - `redirect-gateway ipv6 !ipv4`: indicates IPv6-only default route; openvpn
  //   client will set only routes (for IPv6) but no `redirect-gateway` in env
  //   variables.
  // - `redirect-gateway ipv6`: indicates default route for both v4 and v6;
  //   openvpn client will set both `redirect-gateway` and routes (for IPv6) in
  //   env variables.
  if (has_ipv4) {
    network_config->ipv4_address =
        net_base::IPv4CIDR::CreateFromAddressAndPrefix(
            *ipv4_local,
            ipv4_prefix.value_or(net_base::IPv4CIDR::kMaxPrefixLength));
    if (ipv4_remote.has_value()) {
      // --topology net30 or p2p will set ifconfig_remote

      // Setting a point-to-point address in the kernel will create a route
      // in RT_TABLE_MAIN instead of our per-device table.  To avoid this,
      // create an explicit host route here.
      network_config->included_route_prefixes.push_back(
          *net_base::IPCIDR::CreateFromAddressAndPrefix(
              net_base::IPAddress(*ipv4_remote),
              net_base::IPv4CIDR::kMaxPrefixLength));
    } else if (ipv4_prefix.has_value() &&
               ipv4_prefix != net_base::IPv4CIDR::kMaxPrefixLength) {
      // --topology subnet will set ifconfig_netmask instead
      network_config->included_route_prefixes.push_back(
          net_base::IPCIDR(network_config->ipv4_address->GetPrefixCIDR()));
    }
    network_config->ipv4_default_route = ipv4_redirect_gateway;
    network_config->ipv6_blackhole_route = ipv4_redirect_gateway && !has_ipv6;
  }
  if (has_ipv6) {
    const net_base::IPv6CIDR ipv6_addr =
        net_base::IPv6CIDR::CreateFromAddressAndPrefix(
            *ipv6_local,
            ipv6_prefix.value_or(net_base::IPv6CIDR::kMaxPrefixLength))
            .value();
    network_config->ipv6_addresses.push_back(ipv6_addr);
    if (ipv6_prefix.has_value() &&
        ipv6_prefix != net_base::IPv6CIDR::kMaxPrefixLength) {
      // --topology subnet will set ifconfig_netmask instead
      network_config->included_route_prefixes.push_back(
          net_base::IPCIDR(network_config->ipv6_addresses[0].GetPrefixCIDR()));
    }
  }

  // Parse IPv4 and IPv6 routes from |configuration|.
  std::vector<net_base::IPCIDR> ipv4_routes =
      ParseIPv4RouteOptions(configuration);
  std::vector<net_base::IPCIDR> ipv6_routes =
      ParseIPv6RouteOptions(configuration);
  // Add routes to |included_route_prefixes|.
  std::copy(ipv4_routes.begin(), ipv4_routes.end(),
            std::back_inserter(network_config->included_route_prefixes));
  std::copy(ipv6_routes.begin(), ipv6_routes.end(),
            std::back_inserter(network_config->included_route_prefixes));
  return network_config;
}

namespace {
bool ParseForeignOption(const std::string& option,
                        std::vector<std::string>* domain_search,
                        std::vector<net_base::IPAddress>* dns_servers) {
  SLOG(2) << __func__ << "(" << option << ")";
  const auto tokens = base::SplitStringPiece(option, " ", base::TRIM_WHITESPACE,
                                             base::SPLIT_WANT_ALL);
  if (tokens.size() != 3 ||
      !base::EqualsCaseInsensitiveASCII(tokens[0], "dhcp-option")) {
    return false;
  }
  if (base::EqualsCaseInsensitiveASCII(tokens[1], "domain")) {
    domain_search->push_back(std::string(tokens[2]));
    return true;
  } else if (base::EqualsCaseInsensitiveASCII(tokens[1], "dns")) {
    const std::optional<net_base::IPAddress> dns =
        net_base::IPAddress::CreateFromString(tokens[2]);
    if (!dns.has_value()) {
      LOG(WARNING) << "Failed to parse DNS " << tokens[2];
      return false;
    }
    dns_servers->push_back(*dns);
    return true;
  }
  return false;
}
}  // namespace

// static
void OpenVPNDriver::ParseForeignOptions(
    const ForeignOptions& options,
    std::vector<std::string>* domain_search,
    std::vector<net_base::IPAddress>* dns_servers) {
  domain_search->clear();
  dns_servers->clear();
  for (const auto& option_map : options) {
    if (!ParseForeignOption(option_map.second, domain_search, dns_servers)) {
      LOG(INFO) << "Ignore foreign option " << option_map.second;
    }
  }
}

namespace {
struct RouteOption {
  std::string_view prefix;
  int index;
};

// Tries to parse a key as a route option. If the key follow the format
// {network,netmask,prefix}_<index>, then {network_,netmask_,prefix_} will be
// returned as |prefix| and <index> will be returned as |index|. Otherwise
// std::nullopt will be returned.
std::optional<RouteOption> ParseKeyAsRouteOption(std::string_view key) {
  RouteOption ret;
  for (const auto& prefix :
       {kOpenVPNRouteNetworkPrefix, kOpenVPNRouteNetmaskPrefix,
        kOpenVPNRouteGatewayPrefix}) {
    if (base::StartsWith(key, prefix, base::CompareCase::INSENSITIVE_ASCII)) {
      ret.prefix = prefix;
      break;
    }
  }
  if (ret.prefix.empty() ||
      !base::StringToInt(key.substr(ret.prefix.size()), &ret.index)) {
    return std::nullopt;
  }
  return std::make_optional(ret);
}
}  // namespace

// static
std::vector<net_base::IPCIDR> OpenVPNDriver::ParseIPv4RouteOptions(
    const std::map<std::string, std::string>& configuration) {
  struct IPv4Route {
    std::optional<net_base::IPv4Address> network;
    int prefix_length = 0;
    std::optional<net_base::IPv4Address> gateway;
  };

  // Temporarily store the parsed routes here. The key is the route index.
  base::flat_map<int, IPv4Route> routes;

  for (const auto& [key, value] : configuration) {
    // Keys for IPv4 routes start with route_ while those for IPv6 routes starts
    // with route_ipv6_. As we are parsing IPv4 routes here, we need to drop
    // those for IPv6 routes.
    if (!base::StartsWith(key, kOpenVPNRouteOptionPrefix,
                          base::CompareCase::INSENSITIVE_ASCII) ||
        base::StartsWith(key, kOpenVPNRouteIPv6OptionPrefix,
                         base::CompareCase::INSENSITIVE_ASCII)) {
      continue;
    }
    // These options are unused. Catch them here so that they don't get passed
    // to ParseKeyAsRouteOption().
    if (base::EqualsCaseInsensitiveASCII(key, kOpenVPNRouteNetGateway) ||
        base::EqualsCaseInsensitiveASCII(key, kOpenVPNRouteVPNGateway)) {
      continue;
    }
    // The format of keys for IPv4 routes is
    // route_{network,netmask,gateway}_<index>.
    std::optional<RouteOption> route_option =
        ParseKeyAsRouteOption(key.substr(kOpenVPNRouteOptionPrefix.size()));
    if (!route_option.has_value()) {
      LOG(WARNING) << "Route option ignored: " << key;
      continue;
    }
    const std::optional<net_base::IPv4Address> addr =
        net_base::IPv4Address::CreateFromString(value);
    if (!addr.has_value()) {
      LOG(WARNING) << "Failed to get address from " << value
                   << " for route option " << key;
      continue;
    }
    IPv4Route& route = routes[route_option->index];
    if (route_option->prefix == kOpenVPNRouteNetworkPrefix) {
      route.network = addr;
    } else if (route_option->prefix == kOpenVPNRouteNetmaskPrefix) {
      route.prefix_length =
          net_base::IPv4CIDR::GetPrefixLength(*addr).value_or(0);
      if (route.prefix_length == 0) {
        LOG(WARNING) << "Failed to get prefix length from " << value;
      }
    } else {
      // route_option->prefix == kOpenVPNRouteGatewayPrefix.
      route.gateway = addr;
    }
  }

  // Build routes with the temporary |routes|.
  std::vector<net_base::IPCIDR> ret;
  for (const auto& [index, route] : routes) {
    if (!route.network.has_value() || !route.gateway.has_value()) {
      LOG(WARNING) << "Ignoring incomplete route: " << index;
      continue;
    }
    const std::optional<net_base::IPCIDR> cidr =
        net_base::IPCIDR::CreateFromAddressAndPrefix(
            net_base::IPAddress(*route.network), route.prefix_length);
    if (!cidr.has_value()) {
      LOG(WARNING) << "Ignoring invalid route: " << *route.network << "/"
                   << route.prefix_length;
      continue;
    }
    ret.push_back(*cidr);
  }

  return ret;
}

// static
std::vector<net_base::IPCIDR> OpenVPNDriver::ParseIPv6RouteOptions(
    const std::map<std::string, std::string>& configuration) {
  struct IPv6Route {
    std::optional<net_base::IPv6CIDR> network;
    std::optional<net_base::IPv6Address> gateway;
  };
  // Temporarily store the parsed routes here. The key is the route index.
  base::flat_map<int, IPv6Route> routes;

  for (const auto& [key, value] : configuration) {
    if (!base::StartsWith(key, kOpenVPNRouteIPv6OptionPrefix,
                          base::CompareCase::INSENSITIVE_ASCII)) {
      continue;
    }
    // The format of keys for IPv6 routes is
    // route_ipv6_{network,gateway}_<index>.
    std::optional<RouteOption> route_option =
        ParseKeyAsRouteOption(key.substr(kOpenVPNRouteIPv6OptionPrefix.size()));
    if (!route_option.has_value()) {
      LOG(WARNING) << "Route option ignored: " << key;
      continue;
    }
    IPv6Route& route = routes[route_option->index];
    if (route_option->prefix == kOpenVPNRouteNetworkPrefix) {
      route.network = net_base::IPv6CIDR::CreateFromCIDRString(value);
      if (!route.network.has_value()) {
        LOG(WARNING) << "Failed to get network from " << value;
      }
    } else if (route_option->prefix == kOpenVPNRouteGatewayPrefix) {
      route.gateway = net_base::IPv6Address::CreateFromString(value);
      if (!route.gateway.has_value()) {
        LOG(WARNING) << "Failed to get gateway from " << value;
      }
    } else {
      // route_option->prefix == kOpenVPNRouteNetmaskPrefix, which should not
      // exist for IPv6 routes.
      LOG(WARNING) << "Route option ignored: " << key;
    }
  }

  // Build routes with the temporary |routes|.
  std::vector<net_base::IPCIDR> ret;
  for (const auto& [index, route] : routes) {
    if (!route.network.has_value() || !route.gateway.has_value()) {
      LOG(WARNING) << "Ignoring incomplete route: " << index;
      continue;
    }
    ret.push_back(net_base::IPCIDR(*route.network));
  }

  return ret;
}

// static
bool OpenVPNDriver::SplitPortFromHost(std::string_view host,
                                      std::string* name,
                                      std::string* port) {
  const auto tokens = base::SplitStringPiece(host, ":", base::TRIM_WHITESPACE,
                                             base::SPLIT_WANT_ALL);
  int port_number = 0;
  if (tokens.size() != 2 || tokens[0].empty() || tokens[1].empty() ||
      !base::IsAsciiDigit(tokens[1][0]) ||
      !base::StringToInt(tokens[1], &port_number) ||
      port_number > std::numeric_limits<uint16_t>::max()) {
    return false;
  }
  *name = tokens[0];
  *port = tokens[1];
  return true;
}

base::TimeDelta OpenVPNDriver::ConnectAsync(EventHandler* handler) {
  event_handler_ = handler;
  if (!manager()->device_info()->CreateTunnelInterface(base::BindOnce(
          &OpenVPNDriver::OnLinkReady, weak_factory_.GetWeakPtr()))) {
    dispatcher()->PostTask(
        FROM_HERE,
        base::BindOnce(&OpenVPNDriver::FailService, weak_factory_.GetWeakPtr(),
                       Service::kFailureInternal,
                       "Could not create tunnel interface."));
    return kTimeoutNone;
  }
  return kConnectTimeout;
}

void OpenVPNDriver::OnLinkReady(const std::string& link_name,
                                int interface_index) {
  if (!event_handler_) {
    LOG(ERROR) << "event_handler_ is not set";
    return;
  }
  interface_name_ = link_name;
  interface_index_ = interface_index;
  rpc_task_.reset(new RpcTask(control_interface(), this));
  if (!SpawnOpenVPN()) {
    FailService(Service::kFailureInternal, Service::kErrorDetailsNone);
  }
}

void OpenVPNDriver::InitOptions(std::vector<std::vector<std::string>>* options,
                                Error* error) {
  const auto vpnhost = args()->Lookup<std::string>(kProviderHostProperty, "");
  if (vpnhost.empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "VPN host not specified.");
    return;
  }
  AppendOption("client", options);
  AppendOption("tls-client", options);

  AppendRemoteOption(vpnhost, options);
  if (args()->Contains<std::vector<std::string>>(kOpenVPNExtraHostsProperty)) {
    for (const auto& host :
         args()->Get<std::vector<std::string>>(kOpenVPNExtraHostsProperty)) {
      AppendRemoteOption(host, options);
    }
  }
  AppendOption("mark", "1280",
               options);  // 0x500: source type = 5 (Built-in VPN)
  AppendOption("nobind", options);
  AppendOption("persist-key", options);
  AppendOption("persist-tun", options);

  if (interface_name_.empty()) {
    LOG(DFATAL) << "Tunnel interface name needs to be set before connecting.";
    Error::PopulateAndLog(FROM_HERE, error, Error::kInternalError,
                          "Invalid tunnel interface");
    return;
  }
  AppendOption("dev", interface_name_, options);
  AppendOption("dev-type", "tun", options);

  InitLoggingOptions(options);

  AppendValueOption(kVPNMTUProperty, "mtu", options);
  AppendValueOption(kOpenVPNProtoProperty, "proto", options);
  AppendValueOption(kOpenVPNPortProperty, "port", options);
  AppendValueOption(kOpenVPNTLSAuthProperty, "tls-auth", options);
  {
    const auto contents =
        args()->Lookup<std::string>(kOpenVPNTLSAuthContentsProperty, "");
    if (!contents.empty()) {
      if (!vpn_util_->PrepareConfigDirectory(openvpn_config_directory_) ||
          !base::CreateTemporaryFileInDir(openvpn_config_directory_,
                                          &tls_auth_file_) ||
          !vpn_util_->WriteConfigFile(tls_auth_file_, contents)) {
        Error::PopulateAndLog(FROM_HERE, error, Error::kInternalError,
                              "Unable to setup tls-auth file.");
        return;
      }
      AppendOption("tls-auth", tls_auth_file_.value(), options);
    }
  }

  if (args()->Contains<std::string>(kOpenVPNTLSVersionMinProperty)) {
    AppendOption("tls-version-min",
                 args()->Get<std::string>(kOpenVPNTLSVersionMinProperty),
                 options);
  }

  const auto tls_remote =
      args()->Lookup<std::string>(kOpenVPNTLSRemoteProperty, "");
  if (!tls_remote.empty()) {
    AppendOption("verify-x509-name", tls_remote, "name-prefix", options);
  }

  AppendValueOption(kOpenVPNCipherProperty, "cipher", options);
  AppendValueOption(kOpenVPNAuthProperty, "auth", options);
  AppendFlag(kOpenVPNAuthNoCacheProperty, "auth-nocache", options);
  AppendValueOption(kOpenVPNAuthRetryProperty, "auth-retry", options);
  AppendFlag(kOpenVPNCompLZOProperty, "comp-lzo", options);
  AppendFlag(kOpenVPNCompNoAdaptProperty, "comp-noadapt", options);
  AppendValueOption(kOpenVPNCompressProperty, "compress", options);
  AppendFlag(kOpenVPNPushPeerInfoProperty, "push-peer-info", options);
  AppendValueOption(kOpenVPNRenegSecProperty, "reneg-sec", options);
  AppendValueOption(kOpenVPNShaperProperty, "shaper", options);
  AppendValueOption(kOpenVPNServerPollTimeoutProperty, "server-poll-timeout",
                    options);

  if (!InitCAOptions(options, error)) {
    return;
  }

  // Additional remote certificate verification options.
  InitCertificateVerifyOptions(options);
  if (!InitExtraCertOptions(options, error)) {
    return;
  }

  // Client-side ping support.
  AppendValueOption(kOpenVPNPingProperty, "ping", options);
  AppendValueOption(kOpenVPNPingExitProperty, "ping-exit", options);
  AppendValueOption(kOpenVPNPingRestartProperty, "ping-restart", options);

  AppendValueOption(kOpenVPNNsCertTypeProperty, "ns-cert-type", options);

  InitClientAuthOptions(options);
  InitPKCS11Options(options);

  // TLS support.
  auto remote_cert_tls =
      args()->Lookup<std::string>(kOpenVPNRemoteCertTLSProperty, "");
  if (remote_cert_tls.empty()) {
    remote_cert_tls = "server";
  }
  if (remote_cert_tls != "none") {
    AppendOption("remote-cert-tls", remote_cert_tls, options);
  }

  AppendValueOption(kOpenVPNKeyDirectionProperty, "key-direction", options);
  AppendValueOption(kOpenVPNRemoteCertEKUProperty, "remote-cert-eku", options);
  AppendDelimitedValueOption(kOpenVPNRemoteCertKUProperty, "remote-cert-ku",
                             ' ', options);

  if (!InitManagementChannelOptions(options, error)) {
    return;
  }

  // Setup openvpn-script options and RPC information required to send back
  // Layer 3 configuration.
  AppendOption("setenv", kRpcTaskServiceVariable,
               rpc_task_->GetRpcConnectionIdentifier().value(), options);
  AppendOption("setenv", kRpcTaskPathVariable,
               rpc_task_->GetRpcIdentifier().value(), options);
  AppendOption("script-security", "2", options);
  AppendOption("up", kOpenVPNScript, options);
  AppendOption("up-restart", options);

  // Disable openvpn handling since we do route+ifconfig work.
  AppendOption("route-noexec", options);
  AppendOption("ifconfig-noexec", options);

  // The default tx queue length set by openvpn (100) MAY be too small. We used
  // to use the default value set by Linux (500) before, so explicitly set it
  // here to avoid potential performance regression (also see
  // b/313521559#comment2).
  AppendOption("txqueuelen", "500", options);
}

bool OpenVPNDriver::InitCAOptions(
    std::vector<std::vector<std::string>>* options, Error* error) {
  std::vector<std::string> ca_cert_pem;
  if (args()->Contains<std::vector<std::string>>(kOpenVPNCaCertPemProperty)) {
    ca_cert_pem =
        args()->Get<std::vector<std::string>>(kOpenVPNCaCertPemProperty);
  }
  if (ca_cert_pem.empty()) {
    // Use default CAs if no CA certificate is provided.
    AppendOption("ca", kDefaultCACertificates, options);
    return true;
  }

  const base::FilePath certfile =
      certificate_file_->CreatePEMFromStrings(ca_cert_pem);
  if (certfile.empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "Unable to extract PEM CA certificates.");
    return false;
  }
  AppendOption("ca", certfile.value(), options);
  return true;
}

void OpenVPNDriver::InitCertificateVerifyOptions(
    std::vector<std::vector<std::string>>* options) {
  AppendValueOption(kOpenVPNVerifyHashProperty, "verify-hash", options);
  const auto x509_name =
      args()->Lookup<std::string>(kOpenVPNVerifyX509NameProperty, "");
  if (!x509_name.empty()) {
    const auto x509_type =
        args()->Lookup<std::string>(kOpenVPNVerifyX509TypeProperty, "");
    if (x509_type.empty()) {
      AppendOption("verify-x509-name", x509_name, options);
    } else {
      AppendOption("verify-x509-name", x509_name, x509_type, options);
    }
  }
}

bool OpenVPNDriver::InitExtraCertOptions(
    std::vector<std::vector<std::string>>* options, Error* error) {
  if (!args()->Contains<std::vector<std::string>>(
          kOpenVPNExtraCertPemProperty)) {
    // It's okay for this parameter to be unspecified.
    return true;
  }

  const auto extra_certs =
      args()->Get<std::vector<std::string>>(kOpenVPNExtraCertPemProperty);
  if (extra_certs.empty()) {
    // It's okay for this parameter to be empty.
    return true;
  }

  const base::FilePath certfile =
      extra_certificates_file_->CreatePEMFromStrings(extra_certs);
  if (certfile.empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "Unable to extract extra PEM CA certificates.");
    return false;
  }

  AppendOption("extra-certs", certfile.value(), options);
  return true;
}

void OpenVPNDriver::InitPKCS11Options(
    std::vector<std::vector<std::string>>* options) {
  const auto id = args()->Lookup<std::string>(kOpenVPNClientCertIdProperty, "");
  if (!id.empty()) {
    AppendOption("pkcs11-providers", kDefaultPKCS11Provider, options);
    AppendOption("pkcs11-id", id, options);
  }
}

void OpenVPNDriver::InitClientAuthOptions(
    std::vector<std::vector<std::string>>* options) {
  // If the AuthUserPass property is set, or the User property is non-empty, or
  // a client cert was not provided, specify user-password client
  // authentication.
  if (args()->Contains<std::string>(kOpenVPNAuthUserPassProperty) ||
      !args()->Lookup<std::string>(kOpenVPNUserProperty, "").empty() ||
      args()->Lookup<std::string>(kOpenVPNClientCertIdProperty, "").empty()) {
    AppendOption("auth-user-pass", options);
  }
}

bool OpenVPNDriver::InitManagementChannelOptions(
    std::vector<std::vector<std::string>>* options, Error* error) {
  if (!management_server_->Start(options)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInternalError,
                          "Unable to setup management channel.");
    return false;
  }
  // If there's a connected default service already, allow the openvpn client to
  // establish connection as soon as it's started. Otherwise, hold the client
  // until an underlying service connects and OnDefaultServiceChanged is
  // invoked.
  if (manager()->IsConnected()) {
    management_server_->ReleaseHold();
  }
  return true;
}

void OpenVPNDriver::InitLoggingOptions(
    std::vector<std::vector<std::string>>* options) {
  AppendOption("syslog", options);

  const auto verb = args()->Lookup<std::string>(kOpenVPNVerbProperty, "");
  if (!verb.empty()) {
    AppendOption("verb", verb, options);
    return;
  }

  if (SLOG_IS_ON(VPN, 6)) {
    // Maximum output:
    // --verb 9 enables PKCS11 debug, TCP stream, link read/write
    // --verb 8 enables event waits, scheduler, tls_session
    AppendOption("verb", "9", options);
  } else if (SLOG_IS_ON(VPN, 5)) {
    // --verb 7 enables data channel encryption keys, routing,
    // pkcs11 actions, pings, push/pull debug
    AppendOption("verb", "7", options);
  } else if (SLOG_IS_ON(VPN, 4)) {
    // --verb 6 enables tcp/udp reads/writes (short), tun/tap reads/writes
    // --verb 5 enables printing 'R' or 'W' per packet to stdout
    AppendOption("verb", "6", options);
  } else if (SLOG_IS_ON(VPN, 3)) {
    // --verb 4 enables logging packet drops, options
    AppendOption("verb", "4", options);
  } else if (SLOG_IS_ON(VPN, 0)) {
    // --verb 3 is the old default for `ff_debug +vpn`
    AppendOption("verb", "3", options);
  }
}

void OpenVPNDriver::AppendOption(
    std::string_view option, std::vector<std::vector<std::string>>* options) {
  options->push_back({std::string(option)});
}

void OpenVPNDriver::AppendOption(
    std::string_view option,
    std::string_view value,
    std::vector<std::vector<std::string>>* options) {
  options->push_back({std::string(option), std::string(value)});
}

void OpenVPNDriver::AppendOption(
    std::string_view option,
    std::string_view value0,
    std::string_view value1,
    std::vector<std::vector<std::string>>* options) {
  options->push_back(
      {std::string(option), std::string(value0), std::string(value1)});
}

void OpenVPNDriver::AppendRemoteOption(
    std::string_view host, std::vector<std::vector<std::string>>* options) {
  std::string host_name, host_port;
  if (SplitPortFromHost(host, &host_name, &host_port)) {
    DCHECK(!host_name.empty());
    DCHECK(!host_port.empty());
    AppendOption("remote", host_name, host_port, options);
  } else {
    AppendOption("remote", host, options);
  }
}

bool OpenVPNDriver::AppendValueOption(
    std::string_view property,
    std::string_view option,
    std::vector<std::vector<std::string>>* options) {
  const auto value = args()->Lookup<std::string>(property, "");
  if (!value.empty()) {
    AppendOption(option, value, options);
    return true;
  }
  return false;
}

bool OpenVPNDriver::AppendDelimitedValueOption(
    const std::string& property,
    const std::string& option,
    char delimiter,
    std::vector<std::vector<std::string>>* options) {
  const auto value = args()->Lookup<std::string>(property, "");
  if (!value.empty()) {
    auto parts = base::SplitString(value, std::string{delimiter},
                                   base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
    parts.insert(parts.begin(), option);
    options->push_back(std::move(parts));
    return true;
  }
  return false;
}

bool OpenVPNDriver::AppendFlag(const std::string& property,
                               const std::string& option,
                               std::vector<std::vector<std::string>>* options) {
  if (args()->Contains<std::string>(property)) {
    AppendOption(option, options);
    return true;
  }
  return false;
}

void OpenVPNDriver::Disconnect() {
  SLOG(2) << __func__;
  Cleanup();
  event_handler_ = nullptr;
}

void OpenVPNDriver::OnConnectTimeout() {
  Service::ConnectFailure failure =
      management_server_->state() == OpenVPNManagementServer::kStateResolve
          ? Service::kFailureDNSLookup
          : Service::kFailureConnect;
  FailService(failure, Service::kErrorDetailsNone);
}

void OpenVPNDriver::OnReconnecting(ReconnectReason reason) {
  LOG(INFO) << __func__ << "(" << reason << ")";
  if (!event_handler_) {
    LOG(ERROR) << "event_handler_ is not set";
    return;
  }
  base::TimeDelta timeout = GetReconnectTimeout(reason);
  event_handler_->OnDriverReconnecting(timeout);
}

// static
base::TimeDelta OpenVPNDriver::GetReconnectTimeout(ReconnectReason reason) {
  switch (reason) {
    case kReconnectReasonOffline:
      return kReconnectOfflineTimeout;
    case kReconnectReasonTLSError:
      return kReconnectTLSErrorTimeout;
    default:
      return kConnectTimeout;
  }
}

KeyValueStore OpenVPNDriver::GetProvider(Error* error) {
  SLOG(2) << __func__;
  KeyValueStore props = VPNDriver::GetProvider(error);
  props.Set<bool>(
      kPassphraseRequiredProperty,
      args()->Lookup<std::string>(kOpenVPNPasswordProperty, "").empty() &&
          args()->Lookup<std::string>(kOpenVPNTokenProperty, "").empty());
  return props;
}

std::vector<std::string> OpenVPNDriver::GetCommandLineArgs() {
  SLOG(2) << __func__ << "(" << lsb_release_file_.value() << ")";
  std::vector<std::string> args = {"--config", openvpn_config_file_.value()};
  std::string contents;
  if (!base::ReadFileToString(lsb_release_file_, &contents)) {
    LOG(ERROR) << "Unable to read the lsb-release file: "
               << lsb_release_file_.value();
    return args;
  }
  const auto lines = base::SplitStringPiece(
      contents, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  for (const std::string_view line : lines) {
    const size_t assign = line.find('=');
    if (assign == std::string::npos) {
      continue;
    }
    const auto key = line.substr(0, assign);
    const std::string value(line.substr(assign + 1));
    if (key == kChromeOSReleaseName) {
      args.push_back("--setenv");
      args.push_back("UV_PLAT");
      args.push_back(value);
    } else if (key == kChromeOSReleaseVersion) {
      args.push_back("--setenv");
      args.push_back("UV_PLAT_REL");
      args.push_back(value);
    }
    // Other LSB release values are irrelevant.
  }
  return args;
}

void OpenVPNDriver::OnDefaultPhysicalServiceEvent(
    DefaultPhysicalServiceEvent event) {
  if (!event_handler_)
    return;

  // When this happens, it means the service is connecting but the management
  // server and the OpenVPN client have not been started yet. We don't need to
  // do anything in this case:
  // 1) For the service-down event, a new started client will be automatically
  //    on hold and we will check if the default service is connected before
  //    releasing the hold (see InitManagementChannelOptions()), and then the
  //    following service-up event will release the hold.
  // 2) For the other two events, it will just set up the VPN connection on the
  //    new physical service.
  if (!management_server_->IsStarted()) {
    LOG(INFO) << "Default physical service event comes before management "
                 "server starts.";
    return;
  }

  switch (event) {
    case DefaultPhysicalServiceEvent::kUp:
      management_server_->ReleaseHold();
      event_handler_->OnDriverReconnecting(
          GetReconnectTimeout(kReconnectReasonOffline));
      break;
    case DefaultPhysicalServiceEvent::kDown:
      management_server_->Hold();
      management_server_->Restart();
      event_handler_->OnDriverReconnecting(kTimeoutNone);
      break;
    case DefaultPhysicalServiceEvent::kChanged:
      // Ask the management server to reconnect immediately.
      management_server_->ReleaseHold();
      management_server_->Restart();
      event_handler_->OnDriverReconnecting(
          GetReconnectTimeout(kReconnectReasonOffline));
      break;
  }
}

void OpenVPNDriver::ReportConnectionMetrics() {
  if (args()->Contains<std::vector<std::string>>(kOpenVPNCaCertPemProperty) &&
      !args()
           ->Get<std::vector<std::string>>(kOpenVPNCaCertPemProperty)
           .empty()) {
    metrics()->SendEnumToUMA(
        Metrics::kMetricVpnRemoteAuthenticationType,
        Metrics::kVpnRemoteAuthenticationTypeOpenVpnCertificate);
  } else {
    metrics()->SendEnumToUMA(
        Metrics::kMetricVpnRemoteAuthenticationType,
        Metrics::kVpnRemoteAuthenticationTypeOpenVpnDefault);
  }

  bool has_user_authentication = false;
  if (args()->Lookup<std::string>(kOpenVPNTokenProperty, "") != "") {
    metrics()->SendEnumToUMA(
        Metrics::kMetricVpnUserAuthenticationType,
        Metrics::kVpnUserAuthenticationTypeOpenVpnUsernameToken);
    has_user_authentication = true;
  }
  if (args()->Lookup<std::string>(kOpenVPNOTPProperty, "") != "") {
    metrics()->SendEnumToUMA(
        Metrics::kMetricVpnUserAuthenticationType,
        Metrics::kVpnUserAuthenticationTypeOpenVpnUsernamePasswordOtp);
    has_user_authentication = true;
  }
  if (args()->Lookup<std::string>(kOpenVPNAuthUserPassProperty, "") != "" ||
      args()->Lookup<std::string>(kOpenVPNUserProperty, "") != "") {
    metrics()->SendEnumToUMA(
        Metrics::kMetricVpnUserAuthenticationType,
        Metrics::kVpnUserAuthenticationTypeOpenVpnUsernamePassword);
    has_user_authentication = true;
  }
  if (args()->Lookup<std::string>(kOpenVPNClientCertIdProperty, "") != "") {
    metrics()->SendEnumToUMA(
        Metrics::kMetricVpnUserAuthenticationType,
        Metrics::kVpnUserAuthenticationTypeOpenVpnCertificate);
    has_user_authentication = true;
  }
  if (!has_user_authentication) {
    metrics()->SendEnumToUMA(Metrics::kMetricVpnUserAuthenticationType,
                             Metrics::kVpnUserAuthenticationTypeOpenVpnNone);
  }
}

void OpenVPNDriver::ReportCipherMetrics(std::string_view cipher) {
  static constexpr auto str2enum =
      base::MakeFixedFlatMap<std::string_view, Metrics::VpnOpenVPNCipher>({
          {"BF-CBC", Metrics::kVpnOpenVPNCipher_BF_CBC},
          {"AES-256-GCM", Metrics::kVpnOpenVPNCipher_AES_256_GCM},
          {"AES-128-GCM", Metrics::kVpnOpenVPNCipher_AES_128_GCM},
      });
  Metrics::VpnOpenVPNCipher metric = Metrics::kVpnOpenVPNCipherUnknown;
  const auto it = str2enum.find(cipher);
  if (it != str2enum.end()) {
    metric = it->second;
  }

  metrics()->SendEnumToUMA(Metrics::kMetricVpnOpenVPNCipher, metric);
}

}  // namespace shill
