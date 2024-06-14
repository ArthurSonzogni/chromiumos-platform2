// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/tethering_manager.h"

#include <math.h>
#include <stdint.h>

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/rand_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/time/time.h>
#include <chromeos/dbus/shill/dbus-constants.h>
#include <chromeos/net-base/ip_address.h>
#include <chromeos/net-base/ipv4_address.h>
#include <chromeos/net-base/ipv6_address.h>
#include <chromeos/net-base/mac_address.h>

#include "shill/cellular/cellular_service_provider.h"
#include "shill/device.h"
#include "shill/error.h"
#include "shill/mac_address.h"
#include "shill/manager.h"
#include "shill/network/network_monitor.h"
#include "shill/network/portal_detector.h"
#include "shill/profile.h"
#include "shill/store/property_accessor.h"
#include "shill/technology.h"
#include "shill/wifi/hotspot_device.h"
#include "shill/wifi/hotspot_service.h"
#include "shill/wifi/wifi.h"
#include "shill/wifi/wifi_phy.h"
#include "shill/wifi/wifi_provider.h"
#include "shill/wifi/wifi_rf.h"

namespace shill {

namespace {

static constexpr char kSSIDPrefix[] = "chromeOS-";
// Random suffix should provide enough uniqueness to have low SSID collision
// possibility, while have enough anonymity among chromeOS population to make
// the device untrackable. Use 4 digit numbers as random SSID suffix.
static constexpr size_t kSSIDSuffixLength = 4;
// Max SSID length is 32 octets, hex encode change 1 character into 2 hex
// digits, thus max hex SSID length is multiplied by 2.
static constexpr size_t kMaxWiFiHexSSIDLength = 32 * 2;
static constexpr size_t kMinWiFiPassphraseLength = 8;
static constexpr size_t kMaxWiFiPassphraseLength = 63;
// Stop tethering and return error if tethering cannot be fully started within
// |kStartTimeout| time. This is the default value that will be used, unless it
// is explicitly updated by the upstream technology handler (e.g. if a complex
// setup that may require a longer timeout is used).
static constexpr base::TimeDelta kStartTimeout = base::Seconds(10);
// Return error if tethering cannot be fully stopped within |kStopTimeout| time.
static constexpr base::TimeDelta kStopTimeout = base::Seconds(5);

// Auto disable tethering if no clients for |kAutoDisableDelay|.
static constexpr base::TimeDelta kAutoDisableDelay = base::Minutes(5);

// Maximum time to wait for the upstream Network to successfully complete
// network validation before disabling the tethering session.
static constexpr base::TimeDelta kUpstreamNetworkValidationTimeout =
    base::Minutes(1);

// Default priority for tethering. Used for legacy API and for cases where
// tethering is restarted and we can't determine the previous priority.
static constexpr WiFiPhy::Priority kDefaultPriority =
    WiFiPhy::Priority(WiFiPhy::Priority::kMaximumPriority);

// Prefix used by tethering logging messages when the tethering session is
// stopped due to unexpected errors. This prefix is used by the anomaly detector
// to identify these events.
constexpr std::string_view kTetheringStopAnomalyDetectorPrefix =
    "Tethering stopped unexpectly due to reason: ";

bool StoreToConfigBool(const StoreInterface* storage,
                       const std::string& storage_id,
                       KeyValueStore* config,
                       const std::string& name) {
  bool bool_val;
  if (!storage->GetBool(storage_id, name, &bool_val))
    return false;

  config->Set<bool>(name, bool_val);
  return true;
}
bool StoreToConfigString(const StoreInterface* storage,
                         const std::string& storage_id,
                         KeyValueStore* config,
                         const std::string& name) {
  std::string string_val;
  if (!storage->GetString(storage_id, name, &string_val))
    return false;

  config->Set<std::string>(name, string_val);
  return true;
}

// Gets the DHCP options for starting the IPv4 DHCP server at the downstream.
// Returns std::nullopt if the upstream is an IPv6-only network.
std::optional<patchpanel::Client::DHCPOptions> GetDHCPOptions(
    const Network& network, const Service& service) {
  const auto network_config = network.GetNetworkConfig();
  // Checks if upstream has IPv4 configuration and it's ready. If not, then we
  // don't start the DHCP server.
  if (!network_config.ipv4_address) {
    return std::nullopt;
  }

  patchpanel::Client::DHCPOptions options;
  // Fill the DNS servers.
  for (const auto& dns_server : network_config.dns_servers) {
    const auto dns_server_ipv4 = dns_server.ToIPv4Address();
    if (dns_server_ipv4) {
      options.dns_server_addresses.push_back(*dns_server_ipv4);
    }
  }

  // Fill the list of domain search.
  options.domain_search_list = network_config.dns_search_domains;

  // Set the flag for "ANDROID_METERED" option.
  options.is_android_metered = service.IsMetered();
  return options;
}

// b/294287313: When the uplink Network is a Cellular secondary multiplexed PDN,
// TetheringManager must pass to patchpanel the IPv6 configuration of the uplink
// Network explicitly.
std::optional<patchpanel::Client::UplinkIPv6Configuration>
GetUplinkIPv6Configuration(const Network& network) {
  // Only consider uplink Cellular Networks.
  if (network.technology() != Technology::kCellular) {
    return std::nullopt;
  }
  std::optional<net_base::IPv6CIDR> uplink_ipv6_cidr;
  for (const auto& addr : network.GetAddresses()) {
    if (addr.GetFamily() == net_base::IPFamily::kIPv6) {
      uplink_ipv6_cidr = addr.ToIPv6CIDR();
      break;
    }
  }
  // Check if the Network has an IPv6 configuration.
  if (!uplink_ipv6_cidr) {
    return std::nullopt;
  }
  patchpanel::Client::UplinkIPv6Configuration uplink_ipv6_config;
  uplink_ipv6_config.uplink_address = *uplink_ipv6_cidr;
  for (const auto& dns_server : network.GetDNSServers()) {
    if (dns_server.GetFamily() == net_base::IPFamily::kIPv6) {
      uplink_ipv6_config.dns_server_addresses.push_back(
          *dns_server.ToIPv6Address());
    }
  }
  return uplink_ipv6_config;
}

}  // namespace

TetheringManager::TetheringManager(Manager* manager)
    : manager_(manager),
      allowed_(false),
      experimental_tethering_functionality_(false),
      state_(TetheringState::kTetheringIdle),
      upstream_network_(nullptr),
      upstream_service_(nullptr),
      downstream_network_started_(false),
      hotspot_dev_(nullptr),
      hotspot_service_up_(false),
      stop_reason_(StopReason::kInitial) {
  ResetConfiguration();
}

TetheringManager::~TetheringManager() = default;

void TetheringManager::ResetConfiguration() {
  auto_disable_ = true;
  upstream_technology_ = Technology::kCellular;
  std::string hex_ssid;
  std::string passphrase;

  do {
    uint64_t rand = base::RandInt(pow(10, kSSIDSuffixLength), INT_MAX);
    std::string suffix = std::to_string(rand);
    std::string ssid =
        kSSIDPrefix + suffix.substr(suffix.size() - kSSIDSuffixLength);
    hex_ssid = base::HexEncode(ssid.data(), ssid.size());
  } while (hex_ssid == hex_ssid_);
  hex_ssid_ = hex_ssid;

  do {
    passphrase = base::RandBytesAsString(kMinWiFiPassphraseLength >> 1);
    passphrase = base::HexEncode(passphrase.data(), passphrase.size());
  } while (passphrase == passphrase_);
  passphrase_ = passphrase;

  security_ = WiFiSecurity(WiFiSecurity::kWpa2);
  mar_ = true;
  stable_mac_addr_ = MACAddress::CreateRandom();
  band_ = WiFiBand::kAllBands;
  downstream_device_for_test_ = std::nullopt;
  downstream_phy_index_for_test_ = std::nullopt;
}

void TetheringManager::InitPropertyStore(PropertyStore* store) {
  HelpRegisterDerivedBool(store, kTetheringAllowedProperty,
                          &TetheringManager::GetAllowed,
                          &TetheringManager::SetAllowed);
  HelpRegisterDerivedBool(
      store, kExperimentalTetheringFunctionality,
      &TetheringManager::GetExperimentalTetheringFunctionality,
      &TetheringManager::SetExperimentalTetheringFunctionality);
  store->RegisterDerivedKeyValueStore(
      kTetheringConfigProperty,
      KeyValueStoreAccessor(new CustomAccessor<TetheringManager, KeyValueStore>(
          this, &TetheringManager::GetConfig,
          &TetheringManager::SetAndPersistConfig)));
  store->RegisterDerivedKeyValueStore(
      kTetheringCapabilitiesProperty,
      KeyValueStoreAccessor(new CustomAccessor<TetheringManager, KeyValueStore>(
          this, &TetheringManager::GetCapabilities, nullptr)));
  store->RegisterDerivedKeyValueStore(
      kTetheringStatusProperty,
      KeyValueStoreAccessor(new CustomAccessor<TetheringManager, KeyValueStore>(
          this, &TetheringManager::GetStatus, nullptr)));
}

bool TetheringManager::ToProperties(KeyValueStore* properties) const {
  DCHECK(properties);

  if (hex_ssid_.empty() || passphrase_.empty()) {
    LOG(ERROR) << "Missing SSID or passphrase";
    properties->Clear();
    return false;
  }

  properties->Set<bool>(kTetheringConfAutoDisableProperty, auto_disable_);
  properties->Set<bool>(kTetheringConfMARProperty, mar_);
  properties->Set<std::string>(kTetheringConfSSIDProperty, hex_ssid_);
  properties->Set<std::string>(kTetheringConfPassphraseProperty, passphrase_);
  properties->Set<std::string>(kTetheringConfSecurityProperty,
                               security_.ToString());
  properties->Set<std::string>(kTetheringConfBandProperty, WiFiBandName(band_));
  properties->Set<std::string>(kTetheringConfUpstreamTechProperty,
                               TechnologyName(upstream_technology_));
  if (downstream_device_for_test_.has_value()) {
    properties->Set<std::string>(kTetheringConfDownstreamDeviceForTestProperty,
                                 *downstream_device_for_test_);
  }
  if (downstream_phy_index_for_test_.has_value()) {
    properties->Set<uint32_t>(kTetheringConfDownstreamPhyIndexForTestProperty,
                              *downstream_phy_index_for_test_);
  }

  return true;
}

std::optional<bool> TetheringManager::FromProperties(
    const KeyValueStore& properties) {
  // sanity check.
  auto ssid =
      properties.GetOptionalValue<std::string>(kTetheringConfSSIDProperty);
  if (ssid.has_value() &&
      (ssid->empty() || ssid->length() > kMaxWiFiHexSSIDLength ||
       !std::all_of(ssid->begin(), ssid->end(), ::isxdigit))) {
    LOG(ERROR) << "Invalid SSID provided in tethering config: " << *ssid;
    return std::nullopt;
  }

  auto passphrase = properties.GetOptionalValue<std::string>(
      kTetheringConfPassphraseProperty);
  if (passphrase.has_value() &&
      (passphrase->length() < kMinWiFiPassphraseLength ||
       passphrase->length() > kMaxWiFiPassphraseLength)) {
    LOG(ERROR) << "Passphrase provided in tethering config has invalid length: "
               << *passphrase;
    return std::nullopt;
  }

  std::optional<WiFiSecurity> sec = std::nullopt;
  if (properties.Contains<std::string>(kTetheringConfSecurityProperty)) {
    sec = WiFiSecurity(
        properties.Get<std::string>(kTetheringConfSecurityProperty));
    if (!sec->IsValid() || !(*sec == WiFiSecurity(WiFiSecurity::kWpa2) ||
                             *sec == WiFiSecurity(WiFiSecurity::kWpa3) ||
                             *sec == WiFiSecurity(WiFiSecurity::kWpa2Wpa3))) {
      LOG(ERROR) << "Invalid security mode provided in tethering config: "
                 << *sec;
      return std::nullopt;
    }
  }

  std::optional<WiFiBand> band = std::nullopt;
  if (properties.Contains<std::string>(kTetheringConfBandProperty)) {
    band = WiFiBandFromName(
        properties.Get<std::string>(kTetheringConfBandProperty));
    if (*band == WiFiBand::kUnknownBand) {
      LOG(ERROR) << "Invalid WiFi band: " << *band;
      return std::nullopt;
    }
  }

  std::optional<Technology> tech = std::nullopt;
  if (properties.Contains<std::string>(kTetheringConfUpstreamTechProperty)) {
    tech = TechnologyFromName(
        properties.Get<std::string>(kTetheringConfUpstreamTechProperty));
    // TODO(b/235762746) Add support for WiFi as an upstream technology.
    if (*tech != Technology::kEthernet && *tech != Technology::kCellular) {
      LOG(ERROR) << "Invalid upstream technology provided in tethering config: "
                 << *tech;
      return std::nullopt;
    }
  }

  bool restart = false;
  // update tethering config in this.
  if (properties.Contains<bool>(kTetheringConfAutoDisableProperty) &&
      auto_disable_ !=
          properties.Get<bool>(kTetheringConfAutoDisableProperty)) {
    // If auto disable config changed, reset inactive timer on the fly which
    // does not require session restart.
    auto_disable_ = properties.Get<bool>(kTetheringConfAutoDisableProperty);
    if (state_ == TetheringState::kTetheringActive) {
      (auto_disable_ && GetClientCount() == 0) ? StartInactiveTimer()
                                               : StopInactiveTimer();
    }
  }

  if (properties.Contains<bool>(kTetheringConfMARProperty) &&
      mar_ != properties.Get<bool>(kTetheringConfMARProperty)) {
    mar_ = properties.Get<bool>(kTetheringConfMARProperty);
    restart = true;
  }

  if (ssid.has_value() && hex_ssid_ != *ssid) {
    hex_ssid_ = *ssid;
    restart = true;
  }

  if (passphrase.has_value() && passphrase_ != *passphrase) {
    passphrase_ = *passphrase;
    restart = true;
  }

  if (sec.has_value() && security_ != *sec) {
    security_ = *sec;
    restart = true;
  }

  if (band.has_value() && band_ != *band) {
    band_ = *band;
    restart = true;
  }

  if (tech.has_value() && upstream_technology_ != *tech) {
    upstream_technology_ = *tech;
    restart = true;
  }

  if (properties.Contains<std::string>(
          kTetheringConfDownstreamDeviceForTestProperty) &&
      downstream_device_for_test_ !=
          properties.Get<std::string>(
              kTetheringConfDownstreamDeviceForTestProperty)) {
    downstream_device_for_test_ = properties.Get<std::string>(
        kTetheringConfDownstreamDeviceForTestProperty);
    restart = true;
  }

  if (properties.Contains<uint32_t>(
          kTetheringConfDownstreamPhyIndexForTestProperty) &&
      downstream_phy_index_for_test_ !=
          properties.Get<uint32_t>(
              kTetheringConfDownstreamPhyIndexForTestProperty)) {
    downstream_phy_index_for_test_ = properties.Get<uint32_t>(
        kTetheringConfDownstreamPhyIndexForTestProperty);
    restart = true;
  }

  return restart;
}

KeyValueStore TetheringManager::GetConfig(Error* error) {
  KeyValueStore config;
  if (!ToProperties(&config)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                          "Failed to get TetheringConfig");
  }
  return config;
}

bool TetheringManager::SetAndPersistConfig(const KeyValueStore& config,
                                           Error* error) {
  const auto profile = manager_->ActiveProfile();
  // TODO(b/172224298): prefer using Profile::IsDefault.
  if (profile->GetUser().empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kIllegalOperation,
                          "Tethering is not allowed without user profile");
    return false;
  }

  auto old_ssid = hex_ssid_;
  auto old_upstream_technology = upstream_technology_;
  auto restart_needed = FromProperties(config);
  if (!restart_needed.has_value()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "Invalid tethering configuration");
    return false;
  }
  // If the SSID changes then re-randomize the stable MAC.
  if (hex_ssid_ != old_ssid) {
    stable_mac_addr_ = MACAddress::CreateRandom();
  }

  if (!Save(profile->GetStorage())) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                          "Failed to save config to user profile");
    return false;
  }

  if (restart_needed.value() &&
      (state_ == TetheringState::kTetheringActive ||
       state_ == TetheringState::kTetheringStarting)) {
    bool bypass_upstream = false;
    if (upstream_technology_ == old_upstream_technology &&
        upstream_technology_ == Technology::kCellular) {
      // Do not stop upstream cellular network in session restart if upstream
      // is not changed as PDN switching is costly.
      bypass_upstream = true;
    }
    // StopTetheringSession with StopReason kConfigChange restarts tethering.
    // Need to send D-Bus result first. So defer restart work to event loop.
    manager_->dispatcher()->PostTask(
        FROM_HERE, base::BindOnce(&TetheringManager::StopTetheringSession,
                                  base::Unretained(this),
                                  StopReason::kConfigChange, bypass_upstream));
  }

  return true;
}

KeyValueStore TetheringManager::GetCapabilities(Error* /* error */) {
  return capabilities_;
}

void TetheringManager::SetCapabilities(const KeyValueStore& value) {
  if (capabilities_ == value) {
    return;
  }
  capabilities_ = value;
  manager_->TetheringCapabilitiesChanged(capabilities_);
}

void TetheringManager::RefreshCapabilities() {
  KeyValueStore caps;
  std::vector<std::string> upstream_technologies;
  std::vector<std::string> downstream_technologies;

  // Ethernet is always supported as an upstream technology.
  upstream_technologies.push_back(TechnologyName(Technology::kEthernet));

  if (manager_->cellular_service_provider()->HardwareSupportsTethering(
          experimental_tethering_functionality_))
    upstream_technologies.push_back(TechnologyName(Technology::kCellular));

  // TODO(b/244335143): This should be based on static SoC capability
  // information. Need to revisit this when Shill has a SoC capability
  // database. For now, use the presence of a WiFi phy as a proxy for
  // checking if AP mode is supported.
  auto wifi_phys = manager_->wifi_provider()->GetPhys();
  if (!wifi_phys.empty()) {
    if (wifi_phys.front()->SupportAPMode() &&
        wifi_phys.front()->SupportAPSTAConcurrency()) {
      downstream_technologies.push_back(TechnologyName(Technology::kWiFi));
      // Wi-Fi specific tethering capabilities.
      // TODO(b/273351443) Add WPA2WPA3 and WPA3 security capability to
      // supported chipset.
      caps.Set<Strings>(kTetheringCapSecurityProperty, {kSecurityWpa2});
    }
  }

  caps.Set<Strings>(kTetheringCapUpstreamProperty, upstream_technologies);
  caps.Set<Strings>(kTetheringCapDownstreamProperty, downstream_technologies);
  SetCapabilities(caps);
}

KeyValueStore TetheringManager::GetStatus() {
  KeyValueStore status;
  status.Set<std::string>(kTetheringStatusStateProperty,
                          TetheringStateName(state_));

  if (state_ != TetheringState::kTetheringActive) {
    if (state_ == TetheringState::kTetheringIdle) {
      status.Set<std::string>(kTetheringStatusIdleReasonProperty,
                              StopReasonToString(stop_reason_));
    }
    return status;
  }

  status.Set<std::string>(kTetheringStatusUpstreamTechProperty,
                          TechnologyName(upstream_technology_));
  status.Set<std::string>(kTetheringStatusDownstreamTechProperty, kTypeWifi);

  // Get stations information.
  Stringmaps clients;
  for (const net_base::MacAddress station : hotspot_dev_->GetStations()) {
    Stringmap client;
    client.insert({kTetheringStatusClientMACProperty, station.ToString()});
    // TODO(b/235763170): Get IP address and hostname from patchpanel
    clients.push_back(client);
  }
  status.Set<Stringmaps>(kTetheringStatusClientsProperty, clients);

  return status;
}

size_t TetheringManager::GetClientCount() {
  return (hotspot_dev_ == nullptr) ? 0 : hotspot_dev_->GetStations().size();
}

void TetheringManager::SetState(TetheringState state) {
  if (state_ == state)
    return;

  LOG(INFO) << "State changed from " << state_ << " to " << state;
  state_ = state;

  manager_->TetheringStatusChanged();
}

// static
const char* TetheringManager::TetheringStateName(const TetheringState& state) {
  switch (state) {
    case TetheringState::kTetheringIdle:
      return kTetheringStateIdle;
    case TetheringState::kTetheringStarting:
      return kTetheringStateStarting;
    case TetheringState::kTetheringActive:
      return kTetheringStateActive;
    case TetheringState::kTetheringStopping:
      return kTetheringStateStopping;
    case TetheringState::kTetheringRestarting:
      return kTetheringStateRestarting;
    default:
      NOTREACHED() << "Unhandled tethering state " << static_cast<int>(state);
      return "Invalid";
  }
}

void TetheringManager::Start() {}

void TetheringManager::Stop() {
  StopTetheringSession(StopReason::kUserExit);
}

void TetheringManager::PostSetEnabledResult(SetEnabledResult result) {
  if (result_callback_) {
    manager_->dispatcher()->PostTask(
        FROM_HERE, base::BindOnce(std::move(result_callback_), result));
  }
}

void TetheringManager::CheckAndStartDownstreamTetheredNetwork() {
  if (!hotspot_dev_ || !hotspot_dev_->IsServiceUp()) {
    // Downstream hotspot device or service is not ready.
    if (hotspot_service_up_) {
      // Has already received the kLinkUp event, but device state is not
      // correct, something went wrong. Terminate tethering session.
      LOG(ERROR) << "Has received kLinkUp event from hotspot device but the "
                    "device state is not correct. Terminate tethering session";
      PostSetEnabledResult(SetEnabledResult::kDownstreamWiFiFailure);
      StopTetheringSession(StopReason::kError);
    }
    return;
  }
  CHECK(hotspot_dev_->link_name());
  if (!upstream_network_ || !upstream_service_) {
    return;
  }

  std::string downstream_ifname = *hotspot_dev_->link_name();
  const auto& upstream_ifname = upstream_network_->interface_name();

  std::optional<int> mtu = upstream_network_->GetNetworkConfig().mtu;

  if (downstream_network_started_) {
    LOG(ERROR) << "Request to start downstream network " << downstream_ifname
               << " tethered to " << upstream_ifname << " was already sent";
    PostSetEnabledResult(SetEnabledResult::kFailure);
    StopTetheringSession(StopReason::kError);
    return;
  }

  auto dhcp_options = GetDHCPOptions(*upstream_network_, *upstream_service_);
  auto uplink_ipv6_config = GetUplinkIPv6Configuration(*upstream_network_);
  downstream_network_started_ =
      manager_->patchpanel_client()->CreateTetheredNetwork(
          downstream_ifname, upstream_ifname, dhcp_options, uplink_ipv6_config,
          mtu,
          base::BindOnce(&TetheringManager::OnDownstreamNetworkReady,
                         base::Unretained(this)));
  if (!downstream_network_started_) {
    LOG(ERROR) << "Failed requesting downstream network " << downstream_ifname
               << " tethered to " << upstream_ifname;
    PostSetEnabledResult(SetEnabledResult::kNetworkSetupFailure);
    StopTetheringSession(StopReason::kDownstreamNetDisconnect);
    return;
  }

  LOG(INFO) << "Requested downstream network " << downstream_ifname
            << " tethered to " << upstream_ifname;
}

void TetheringManager::CheckAndPostTetheringStartResult() {
  if (!downstream_network_fd_.is_valid()) {
    return;
  }

  if (!upstream_network_ || !upstream_network_->IsConnected()) {
    PostSetEnabledResult(SetEnabledResult::kUpstreamNetworkNotAvailable);
    StopTetheringSession(StopReason::kUpstreamNotAvailable);
    return;
  }

  SetState(TetheringState::kTetheringActive);
  start_timer_callback_.Cancel();
  if (hotspot_dev_->GetStations().size() == 0) {
    // Kick off inactive timer when tethering session becomes active and no
    // clients are connected.
    StartInactiveTimer();
  }
  // If Internet connectivity has not yet been evaluated, start the network
  // validation timer.
  if (!IsUpstreamNetworkReady()) {
    StartUpstreamNetworkValidationTimer();
  }
  PostSetEnabledResult(SetEnabledResult::kSuccess);
}

void TetheringManager::CheckAndPostTetheringStopResult() {
  if (upstream_network_ != nullptr) {
    return;
  }

  // TODO(b/235762439): Routine to check other tethering modules.

  stop_timer_callback_.Cancel();
  if (state_ == TetheringState::kTetheringRestarting) {
    StartTetheringSession(WiFiPhy::Priority(kDefaultPriority));
    return;
  }

  SetState(TetheringState::kTetheringIdle);
  if (stop_reason_ == StopReason::kClientStop) {
    PostSetEnabledResult(SetEnabledResult::kSuccess);
  }
}

void TetheringManager::OnStartingTetheringTimeout() {
  SetEnabledResult result = SetEnabledResult::kFailure;
  LOG(ERROR) << __func__ << ": tethering session start timed out";

  if (!hotspot_dev_ || !hotspot_dev_->IsServiceUp()) {
    result = SetEnabledResult::kDownstreamWiFiFailure;
  } else if (!upstream_network_) {
    result = SetEnabledResult::kUpstreamNetworkNotAvailable;
  } else if (!upstream_network_->IsConnected()) {
    result = SetEnabledResult::kUpstreamFailure;
  } else if (!downstream_network_fd_.is_valid()) {
    result = SetEnabledResult::kNetworkSetupFailure;
  }
  PostSetEnabledResult(result);
  StopTetheringSession(StopReason::kStartTimeout);
}

void TetheringManager::OnStartingTetheringUpdateTimeout(
    base::TimeDelta timeout) {
  LOG(INFO) << __func__ << ": " << timeout;
  DCHECK(timeout > kStartTimeout);

  if (start_timer_callback_.IsCancelled()) {
    LOG(INFO) << __func__ << ": already cancelled";
    return;
  }

  if (state_ != TetheringState::kTetheringStarting) {
    LOG(INFO) << __func__ << ": not starting";
    return;
  }

  start_timer_callback_.Cancel();
  start_timer_callback_.Reset(base::BindOnce(
      &TetheringManager::OnStartingTetheringTimeout, base::Unretained(this)));
  manager_->dispatcher()->PostDelayedTask(
      FROM_HERE, start_timer_callback_.callback(), timeout);
}

void TetheringManager::FreeUpstreamNetwork() {
  // OnNetworkDestroyed() may have been called during a ReleaseTetheringNetwork
  // call (e.g. if connecting DUN as DEFAULT or a multiplexed DUN).
  if (!upstream_network_) {
    return;
  }
  upstream_network_->UnregisterEventHandler(this);
  upstream_network_ = nullptr;
  upstream_service_ = nullptr;
}

void TetheringManager::OnStoppingTetheringTimeout() {
  LOG(ERROR) << __func__ << ": cannot stop tethering session in "
             << kStopTimeout;

  SetEnabledResult result = SetEnabledResult::kFailure;
  if (upstream_network_ != nullptr) {
    // TODO(b/235762746) Cellular: if the upstream cellular network already
    // exists, use CellularServiceProvider::ReleaseTetheringNetwork() instead.

    // For other types of upstream technology like ethernet or WiFi, there is
    // no particular cleanup other than resetting the internal state.
    FreeUpstreamNetwork();
    result = SetEnabledResult::kUpstreamFailure;
  }

  SetState(TetheringState::kTetheringIdle);
  stop_timer_callback_.Cancel();

  if (stop_reason_ == StopReason::kClientStop) {
    PostSetEnabledResult(result);
  }
}

void TetheringManager::StartTetheringSession(WiFiPhy::Priority priority) {
  stop_reason_ = StopReason::kInitial;
  if (state_ != TetheringState::kTetheringIdle &&
      state_ != TetheringState::kTetheringRestarting) {
    LOG(ERROR) << __func__ << ": unexpected tethering state " << state_;
    PostSetEnabledResult(SetEnabledResult::kWrongState);
    return;
  }

  if (hotspot_dev_ || downstream_network_started_ ||
      downstream_network_fd_.is_valid()) {
    LOG(ERROR) << "Tethering resources are not null when starting tethering "
                  "session.";
    PostSetEnabledResult(SetEnabledResult::kFailure);
    return;
  }

  LOG(INFO) << __func__ << ": in state " << state_;
  // Keep the state if it is restarting.
  if (state_ != TetheringState::kTetheringRestarting) {
    SetState(TetheringState::kTetheringStarting);
  }
  start_timer_callback_.Reset(base::BindOnce(
      &TetheringManager::OnStartingTetheringTimeout, base::Unretained(this)));
  manager_->dispatcher()->PostDelayedTask(
      FROM_HERE, start_timer_callback_.callback(), kStartTimeout);

  // Prepare the downlink hotspot device.
  hotspot_service_up_ = false;
  const net_base::MacAddress mac_address =
      mar_ ? MACAddress::CreateRandom().address().value()
           : stable_mac_addr_.address().value();
  bool request_accepted;
  if (downstream_device_for_test_ && downstream_phy_index_for_test_) {
    request_accepted =
        manager_->wifi_provider()->RequestHotspotDeviceCreationForTest(
            mac_address, *downstream_device_for_test_,
            *downstream_phy_index_for_test_,
            base::BindRepeating(&TetheringManager::OnDownstreamDeviceEvent,
                                base::Unretained(this)));
  } else {
    request_accepted = manager_->wifi_provider()->RequestHotspotDeviceCreation(
        mac_address, band_, security_, priority,
        base::BindRepeating(&TetheringManager::OnDownstreamDeviceEvent,
                            base::Unretained(this)));
  }

  if (!request_accepted) {
    LOG(ERROR) << __func__ << ": WiFi AP interface rejected due to concurrency";
    PostSetEnabledResult(SetEnabledResult::kConcurrencyNotSupported);
    StopTetheringSession(StopReason::kResourceBusy);
    return;
  }
}

void TetheringManager::OnDeviceCreated(HotspotDeviceRefPtr hotspot_dev) {
  if (!result_callback_) {
    LOG(ERROR) << "HotspotDevice was created with no pending callback.";
    return;
  }
  if (!hotspot_dev) {
    LOG(ERROR) << __func__ << ": failed to create a WiFi AP interface";
    PostSetEnabledResult(SetEnabledResult::kDownstreamWiFiFailure);
    StopTetheringSession(StopReason::kDownstreamLinkDisconnect);
    return;
  }
  hotspot_dev_ = hotspot_dev;

  if (upstream_network_) {
    // No need to acquire a new upstream network.
    CheckAndStartDownstreamTetheredNetwork();
    return;
  }

  // Prepare the upstream network.
  if (upstream_technology_ == Technology::kCellular) {
    manager_->cellular_service_provider()->AcquireTetheringNetwork(
        base::BindRepeating(&TetheringManager::OnStartingTetheringUpdateTimeout,
                            base::Unretained(this)),
        base::BindOnce(&TetheringManager::OnUpstreamNetworkAcquired,
                       base::Unretained(this)),
        base::BindRepeating(&TetheringManager::OnCellularUpstreamEvent,
                            base::Unretained(this)),
        experimental_tethering_functionality_);
  } else if (upstream_technology_ == Technology::kEthernet) {
    const auto eth_service = manager_->GetFirstEthernetService();
    const auto upstream_network =
        manager_->FindActiveNetworkFromService(eth_service);
    const auto result = upstream_network
                            ? SetEnabledResult::kSuccess
                            : SetEnabledResult::kUpstreamNetworkNotAvailable;
    OnUpstreamNetworkAcquired(result, upstream_network, eth_service.get());
  } else {
    // TODO(b/235762746) Add support for WiFi as an upstream technology for "usb
    // tethering" and for chipsets that support simultaneous hotspot and station
    // modes.
    LOG(ERROR) << __func__ << ": " << upstream_technology_
               << " not supported as an upstream technology";
    PostSetEnabledResult(SetEnabledResult::kInvalidProperties);
    StopTetheringSession(StopReason::kError);
  }
}

void TetheringManager::OnDeviceCreationFailed() {
  LOG(ERROR) << __func__ << ": failed to create a WiFi AP interface";
  PostSetEnabledResult(SetEnabledResult::kDownstreamWiFiFailure);
  StopTetheringSession(StopReason::kDownstreamLinkDisconnect);
  return;
}

void TetheringManager::OnDownstreamDeviceEnabled() {
  // Prepare the downlink service.
  auto freq = manager_->wifi_provider()
                  ->GetPhyAtIndex(hotspot_dev_->phy_index())
                  ->SelectFrequency(band_);

  if (!freq.has_value()) {
    LOG(ERROR) << __func__ << ": failed to select frequency";
    PostSetEnabledResult(SetEnabledResult::kDownstreamWiFiFailure);
    StopTetheringSession(StopReason::kDownstreamLinkDisconnect);
    return;
  }

  std::unique_ptr<HotspotService> service = std::make_unique<HotspotService>(
      hotspot_dev_, hex_ssid_, passphrase_, security_, freq.value());
  if (!hotspot_dev_->ConfigureService(std::move(service))) {
    LOG(ERROR) << __func__ << ": failed to configure the hotspot service";
    PostSetEnabledResult(SetEnabledResult::kDownstreamWiFiFailure);
    StopTetheringSession(StopReason::kDownstreamLinkDisconnect);
    return;
  }
}

void TetheringManager::StopTetheringSession(StopReason reason,
                                            bool bypass_upstream) {
  if (state_ == TetheringState::kTetheringIdle ||
      state_ == TetheringState::kTetheringStopping) {
    if (reason == StopReason::kClientStop) {
      LOG(ERROR) << __func__ << ": no active or starting tethering session";
      PostSetEnabledResult(SetEnabledResult::kWrongState);
    }
    return;
  }

  if (reason == StopReason::kError ||
      reason == StopReason::kDownstreamLinkDisconnect) {
    LOG(ERROR) << kTetheringStopAnomalyDetectorPrefix
               << StopReasonToString(reason);
  } else {
    LOG(INFO) << __func__ << ": " << StopReasonToString(reason);
  }
  stop_reason_ = reason;
  if (reason == StopReason::kConfigChange) {
    // Restart the tethering session due to config change.
    SetState(TetheringState::kTetheringRestarting);
  } else {
    SetState(TetheringState::kTetheringStopping);
  }
  start_timer_callback_.Cancel();
  StopInactiveTimer();

  // Tear down the downstream network if any.
  // TODO(b/275645124) Add a callback to ensure that the downstream network tear
  // down has finished.
  downstream_network_fd_.reset();
  downstream_network_started_ = false;

  // Remove the downstream device if any.
  if (hotspot_dev_) {
    hotspot_dev_->DeconfigureService();
    manager_->wifi_provider()->DeleteLocalDevice(hotspot_dev_);
    hotspot_dev_ = nullptr;
  }
  hotspot_service_up_ = false;

  if (bypass_upstream && state_ == TetheringState::kTetheringRestarting) {
    // Downstream down, bypass upstream, restart tethering session immediately.
    StartTetheringSession(WiFiPhy::Priority(kDefaultPriority));
    return;
  }
  stop_timer_callback_.Reset(base::BindOnce(
      &TetheringManager::OnStoppingTetheringTimeout, base::Unretained(this)));
  manager_->dispatcher()->PostDelayedTask(
      FROM_HERE, stop_timer_callback_.callback(), kStopTimeout);

  if ((upstream_network_ &&
       upstream_network_->technology() == Technology::kCellular) ||
      (!upstream_network_ && upstream_technology_ == Technology::kCellular)) {
    // If the upstream_network_ is a cellular network type or if the current
    // upstream technology is cellular and the upstream_network_ has not been
    // acquired yet, ask CellularServiceProvider to release it and restore to
    // the original PDN.
    manager_->cellular_service_provider()->ReleaseTetheringNetwork(
        upstream_network_,  // may be nullptr if attempt is ongoing
        base::BindOnce(&TetheringManager::OnUpstreamNetworkReleased,
                       base::Unretained(this)));
    return;
  }

  if (!upstream_network_) {
    CheckAndPostTetheringStopResult();
    return;
  }

  // For other types of upstream technology like ethernet or WiFi, there is
  // no particular cleanup other than resetting the internal state.
  OnUpstreamNetworkReleased(/*is_success=*/true);
}

void TetheringManager::StartInactiveTimer() {
  if (!auto_disable_ || !inactive_timer_callback_.IsCancelled() ||
      state_ != TetheringState::kTetheringActive) {
    return;
  }

  LOG(INFO) << __func__ << ": timer fires in " << kAutoDisableDelay;

  inactive_timer_callback_.Reset(
      base::BindOnce(&TetheringManager::StopTetheringSession,
                     base::Unretained(this), StopReason::kInactive, false));
  manager_->dispatcher()->PostDelayedTask(
      FROM_HERE, inactive_timer_callback_.callback(), kAutoDisableDelay);
}

void TetheringManager::StopInactiveTimer() {
  if (inactive_timer_callback_.IsCancelled()) {
    return;
  }

  inactive_timer_callback_.Cancel();
}

void TetheringManager::StartUpstreamNetworkValidationTimer() {
  if (!upstream_network_validation_timer_callback_.IsCancelled() ||
      state_ != TetheringState::kTetheringActive) {
    return;
  }

  LOG(INFO) << __func__ << ": timer fires in "
            << kUpstreamNetworkValidationTimeout;

  upstream_network_validation_timer_callback_.Reset(base::BindOnce(
      &TetheringManager::StopTetheringSession, base::Unretained(this),
      StopReason::kUpstreamNoInternet, false));
  manager_->dispatcher()->PostDelayedTask(
      FROM_HERE, upstream_network_validation_timer_callback_.callback(),
      kUpstreamNetworkValidationTimeout);
}

void TetheringManager::StopUpstreamNetworkValidationTimer() {
  if (upstream_network_validation_timer_callback_.IsCancelled()) {
    return;
  }

  upstream_network_validation_timer_callback_.Cancel();
}

void TetheringManager::OnPeerAssoc() {
  if (state_ != TetheringState::kTetheringActive) {
    return;
  }

  manager_->TetheringStatusChanged();

  if (GetClientCount() != 0) {
    // At least one station associated with this hotspot, cancel the inactive
    // timer.
    StopInactiveTimer();
  }
}

void TetheringManager::OnPeerDisassoc() {
  if (state_ != TetheringState::kTetheringActive) {
    return;
  }

  manager_->TetheringStatusChanged();

  if (GetClientCount() == 0) {
    // No stations associated with this hotspot, start the inactive timer.
    StartInactiveTimer();
  }
}

void TetheringManager::OnDownstreamDeviceEvent(LocalDevice::DeviceEvent event,
                                               const LocalDevice* device) {
  if (!hotspot_dev_ || hotspot_dev_.get() != device) {
    LOG(ERROR) << "Receive event from unmatched local device: "
               << device->link_name().value_or("(no link_name)");
    return;
  }
  CHECK(device->link_name());
  LOG(INFO) << "TetheringManager received downstream device "
            << *device->link_name() << " event: " << event;

  switch (event) {
    case LocalDevice::DeviceEvent::kInterfaceDisabled:
    case LocalDevice::DeviceEvent::kLinkDown:
      if (state_ == TetheringState::kTetheringStarting) {
        PostSetEnabledResult(SetEnabledResult::kDownstreamWiFiFailure);
      }
      StopTetheringSession(StopReason::kDownstreamLinkDisconnect);
      break;
    case LocalDevice::DeviceEvent::kInterfaceEnabled:
      if (state_ != TetheringState::kTetheringStarting &&
          state_ != TetheringState::kTetheringRestarting) {
        LOG(WARNING) << __func__
                     << ": ignore downstream device event: " << event
                     << " in state: " << state_;
      } else {
        OnDownstreamDeviceEnabled();
      }
      break;
    case LocalDevice::DeviceEvent::kLinkUp:
      hotspot_service_up_ = true;
      if (state_ != TetheringState::kTetheringStarting &&
          state_ != TetheringState::kTetheringRestarting) {
        LOG(WARNING) << __func__
                     << ": ignore downstream device event: " << event
                     << " in state: " << state_;
      } else {
        CheckAndStartDownstreamTetheredNetwork();
      }
      break;
    case LocalDevice::DeviceEvent::kPeerConnected:
      OnPeerAssoc();
      break;
    case LocalDevice::DeviceEvent::kPeerDisconnected:
      OnPeerDisassoc();
      break;
    case LocalDevice::DeviceEvent::kLinkFailure:
    case LocalDevice::DeviceEvent::kNetworkUp:
    case LocalDevice::DeviceEvent::kNetworkDown:
    case LocalDevice::DeviceEvent::kNetworkFailure:
      LOG(WARNING) << "TetheringManager ignored unexpected " << event
                   << " event from downstream device " << *device->link_name();
  }
}

void TetheringManager::OnDownstreamNetworkReady(
    base::ScopedFD downstream_network_fd,
    const patchpanel::Client::DownstreamNetwork& downstream_network) {
  if (state_ != TetheringState::kTetheringStarting &&
      state_ != TetheringState::kTetheringRestarting) {
    LOG(WARNING) << __func__ << ": unexpected tethering state " << state_;
    PostSetEnabledResult(SetEnabledResult::kWrongState);
    StopTetheringSession(StopReason::kError);
    return;
  }

  if (!upstream_network_) {
    LOG(WARNING) << __func__ << ": no upstream network defined";
    PostSetEnabledResult(SetEnabledResult::kUpstreamNetworkNotAvailable);
    StopTetheringSession(StopReason::kUpstreamDisconnect);
    return;
  }

  if (!hotspot_dev_) {
    LOG(WARNING) << __func__ << ": no downstream device defined";
    PostSetEnabledResult(SetEnabledResult::kDownstreamWiFiFailure);
    StopTetheringSession(StopReason::kDownstreamLinkDisconnect);
    return;
  }

  CHECK(hotspot_dev_->link_name());

  std::string downstream_ifname = *hotspot_dev_->link_name();
  const auto& upstream_ifname = upstream_network_->interface_name();
  if (!downstream_network_fd.is_valid()) {
    LOG(ERROR) << "Failed creating downstream network " << downstream_ifname
               << " tethered to " << upstream_ifname;
    PostSetEnabledResult(SetEnabledResult::kNetworkSetupFailure);
    StopTetheringSession(StopReason::kDownstreamNetDisconnect);
    return;
  }

  LOG(INFO) << "Established downstream network network_id="
            << downstream_network.network_id << " on " << downstream_ifname
            << " tethered to " << upstream_ifname;
  downstream_network_fd_ = std::move(downstream_network_fd);
  CheckAndPostTetheringStartResult();
}

void TetheringManager::OnUpstreamNetworkAcquired(SetEnabledResult result,
                                                 Network* network,
                                                 ServiceRefPtr service) {
  if (state_ == TetheringState::kTetheringStopping) {
    // Ignore this event when tethering start is aborted.
    // TODO(b/323251708): cancel this callback when tethering start is aborted.
    return;
  }

  if (result != SetEnabledResult::kSuccess) {
    LOG(ERROR) << __func__ << ": no upstream " << upstream_technology_
               << " Network available";
    PostSetEnabledResult(result);
    StopTetheringSession(StopReason::kUpstreamNotAvailable);
    return;
  }

  DCHECK(network);
  DCHECK(service);
  if (!network->IsConnected()) {
    LOG(ERROR) << __func__ << ": upstream Network was not connected";
    PostSetEnabledResult(SetEnabledResult::kFailure);
    StopTetheringSession(StopReason::kUpstreamDisconnect);
    return;
  }

  // TODO(b/273975270): Restart portal detection if the upstream network does
  // not have Internet access and if portal detection is no currently running.

  DCHECK(!upstream_network_);
  DCHECK(!upstream_service_);
  upstream_network_ = network;
  upstream_network_->RegisterEventHandler(this);
  upstream_service_ = service;
  CheckAndStartDownstreamTetheredNetwork();
}

void TetheringManager::OnUpstreamNetworkReleased(bool is_success) {
  if (!is_success) {
    LOG(ERROR) << __func__ << ": failed to release upstream "
               << upstream_technology_ << " Network.";
  }

  FreeUpstreamNetwork();
  CheckAndPostTetheringStopResult();
}

void TetheringManager::SetEnabled(bool enabled,
                                  SetEnabledResultCallback callback) {
  if (!enabled) {
    Disable(std::move(callback));
    return;
  }
  Enable(kDefaultPriority, std::move(callback));
}

void TetheringManager::Enable(uint32_t priority,
                              SetEnabledResultCallback callback) {
  if (state_ == TetheringState::kTetheringStarting ||
      state_ == TetheringState::kTetheringStopping) {
    // Reject a new action immediately if the previous one is ongoing.
    std::move(callback).Run(SetEnabledResult::kBusy);
    return;
  }

  CHECK(!result_callback_);
  result_callback_ = std::move(callback);

  const auto profile = manager_->ActiveProfile();
  // TODO(b/172224298): prefer using Profile::IsDefault.
  if (profile->GetUser().empty()) {
    LOG(ERROR) << __func__ << ": not allowed without user profile";
    PostSetEnabledResult(SetEnabledResult::kNotAllowed);
    return;
  }

  if (!Save(profile->GetStorage())) {
    LOG(ERROR) << __func__ << ": failed to save config to user profile";
    PostSetEnabledResult(SetEnabledResult::kFailure);
    return;
  }

  StartTetheringSession(WiFiPhy::Priority(priority));
}

void TetheringManager::Disable(SetEnabledResultCallback callback) {
  if (state_ == TetheringState::kTetheringStarting) {
    // Abort tethering start, send result for the start method call first.
    CHECK(result_callback_);
    std::move(result_callback_).Run(SetEnabledResult::kAbort);
  }
  result_callback_ = std::move(callback);
  StopTetheringSession(StopReason::kClientStop);
}

// static
const std::string TetheringManager::SetEnabledResultName(
    SetEnabledResult result) {
  switch (result) {
    case SetEnabledResult::kSuccess:
      return kTetheringEnableResultSuccess;
    case SetEnabledResult::kFailure:
      return kTetheringEnableResultFailure;
    case SetEnabledResult::kNotAllowed:
      return kTetheringEnableResultNotAllowed;
    case SetEnabledResult::kInvalidProperties:
      return kTetheringEnableResultInvalidProperties;
    case SetEnabledResult::kWrongState:
      return kTetheringEnableResultWrongState;
    case SetEnabledResult::kUpstreamNetworkNotAvailable:
      return kTetheringEnableResultUpstreamNotAvailable;
    case SetEnabledResult::kUpstreamFailure:
      return kTetheringEnableResultUpstreamFailure;
    case SetEnabledResult::kDownstreamWiFiFailure:
      return kTetheringEnableResultDownstreamWiFiFailure;
    case SetEnabledResult::kNetworkSetupFailure:
      return kTetheringEnableResultNetworkSetupFailure;
    case SetEnabledResult::kAbort:
      return kTetheringEnableResultAbort;
    case SetEnabledResult::kBusy:
      return kTetheringEnableResultBusy;
    case SetEnabledResult::kConcurrencyNotSupported:
      return kTetheringEnableResultConcurrencyNotSupported;
  }
}

void TetheringManager::CheckReadiness(
    Cellular::EntitlementCheckResultCallback callback) {
  // TODO(b/235762746) Add a selection mode for choosing the current default
  // network as the upstream network.

  // Validate the upstream technology.
  switch (upstream_technology_) {
    // Valid upstream technologies
    case Technology::kCellular:
    case Technology::kEthernet:
      break;
    // Invalid upstream technology.
    case Technology::kWiFi:
      // TODO(b/235762746) Add support for WiFi as an upstream technology.
    default:
      LOG(ERROR) << __func__ << ": not supported for " << upstream_technology_
                 << " technology";
      manager_->dispatcher()->PostTask(
          FROM_HERE,
          base::BindOnce(std::move(callback), EntitlementStatus::kNotAllowed));
      return;
  }

  // Check if there is an "online" network for the selected upstream technology.
  // TODO(b/235762746) Avoid using shill Devices and instead inspects currently
  // connected Services.
  const auto devices = manager_->FilterByTechnology(upstream_technology_);
  if (devices.empty()) {
    LOG(ERROR) << __func__ << ": no Device for " << upstream_technology_;
    manager_->dispatcher()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback),
                       EntitlementStatus::kUpstreamNetworkNotAvailable));
    return;
  }

  // TODO(b/235762746) For WiFi -> WiFi and Ethernet -> Ethernet tethering
  // scenarios, this check needs to take into account which interface is
  // used for the downstream network and which interface provides the
  // upstream network. For now always consider the selected service of the
  // first available device.
  const auto service = devices[0]->selected_service();
  if (!service || !service->IsConnected()) {
    LOG(ERROR) << __func__ << ": no connected Service for "
               << upstream_technology_;
    manager_->dispatcher()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback),
                       EntitlementStatus::kUpstreamNetworkNotAvailable));
    return;
  }

  // When the upstream technology is Cellular, delegate to the Provider.
  if (upstream_technology_ == Technology::kCellular) {
    manager_->cellular_service_provider()->TetheringEntitlementCheck(
        std::move(callback), experimental_tethering_functionality_);
    return;
  }

  // Otherwise for WiFi or Ethernet, there is no other entitlement check.
  manager_->dispatcher()->PostTask(
      FROM_HERE,
      base::BindOnce(std::move(callback), EntitlementStatus::kReady));
}

void TetheringManager::OnCellularUpstreamEvent(
    TetheringManager::CellularUpstreamEvent event) {
  if (upstream_technology_ != Technology::kCellular) {
    LOG(ERROR) << "Unexpected upstream event from cellular received";
    return;
  }
  switch (event) {
    case TetheringManager::CellularUpstreamEvent::kUserNoLongerEntitled:
      if (state_ == TetheringState::kTetheringActive ||
          state_ == TetheringState::kTetheringStarting) {
        LOG(INFO) << "TetheringManager stopping session because user is no "
                     "longer entitled to tether";
        StopTetheringSession(StopReason::kUpstreamDisconnect);
      }
      break;
  }
}

// static
const char* TetheringManager::EntitlementStatusName(EntitlementStatus status) {
  switch (status) {
    case EntitlementStatus::kReady:
      return kTetheringReadinessReady;
    case EntitlementStatus::kNotAllowed:
      return kTetheringReadinessNotAllowed;
    case EntitlementStatus::kNotAllowedByCarrier:
      return kTetheringReadinessNotAllowedByCarrier;
    case EntitlementStatus::kNotAllowedOnFw:
      return kTetheringReadinessNotAllowedOnFw;
    case EntitlementStatus::kNotAllowedOnVariant:
      return kTetheringReadinessNotAllowedOnVariant;
    case EntitlementStatus::kNotAllowedUserNotEntitled:
      return kTetheringReadinessNotAllowedUserNotEntitled;
    case EntitlementStatus::kUpstreamNetworkNotAvailable:
      return kTetheringReadinessUpstreamNetworkNotAvailable;
  }
}

void TetheringManager::LoadConfigFromProfile(const ProfileRefPtr& profile) {
  const StoreInterface* storage = profile->GetConstStorage();
  if (!storage->ContainsGroup(kStorageId)) {
    LOG(INFO) << "Tethering config is not available in the persistent "
                 "store, use default config";
    return;
  }

  if (!Load(storage)) {
    LOG(ERROR) << "Tethering config is corrupted in the persistent store, use "
                  "default config";
    // overwrite the corrupted config in profile with the default one.
    if (!Save(profile->GetStorage())) {
      LOG(ERROR) << "Failed to save config to user profile";
    }
  }

  stop_reason_ = StopReason::kInitial;
}

void TetheringManager::UnloadConfigFromProfile() {
  StopTetheringSession(StopReason::kUserExit);
  ResetConfiguration();
}

bool TetheringManager::Save(StoreInterface* storage) {
  // |storage| should not store kTetheringConfDownstreamDeviceForTestProperty
  // and kTetheringConfDownstreamPhyIndexForTestProperty because the properties
  // are only used for testing.

  storage->SetBool(kStorageId, kTetheringConfAutoDisableProperty,
                   auto_disable_);
  storage->SetBool(kStorageId, kTetheringConfMARProperty, mar_);
  stable_mac_addr_.Save(storage, kStorageId);
  storage->SetString(kStorageId, kTetheringConfSSIDProperty, hex_ssid_);
  storage->SetString(kStorageId, kTetheringConfPassphraseProperty, passphrase_);
  storage->SetString(kStorageId, kTetheringConfSecurityProperty,
                     security_.ToString());
  storage->SetString(kStorageId, kTetheringConfBandProperty,
                     WiFiBandName(band_));
  storage->SetString(kStorageId, kTetheringConfUpstreamTechProperty,
                     TechnologyName(upstream_technology_));
  return storage->Flush();
}

bool TetheringManager::Load(const StoreInterface* storage) {
  // We should not load kTetheringConfDownstreamDeviceForTestProperty and
  // kTetheringConfDownstreamPhyIndexForTestProperty from |storage| because the
  // properties are only used for testing.

  KeyValueStore config;
  bool valid;
  valid = StoreToConfigBool(storage, kStorageId, &config,
                            kTetheringConfAutoDisableProperty);
  valid = valid && StoreToConfigBool(storage, kStorageId, &config,
                                     kTetheringConfMARProperty);
  valid = valid && StoreToConfigString(storage, kStorageId, &config,
                                       kTetheringConfSSIDProperty);
  valid = valid && StoreToConfigString(storage, kStorageId, &config,
                                       kTetheringConfPassphraseProperty);
  valid = valid && StoreToConfigString(storage, kStorageId, &config,
                                       kTetheringConfSecurityProperty);
  valid = valid && StoreToConfigString(storage, kStorageId, &config,
                                       kTetheringConfBandProperty);
  valid = valid && StoreToConfigString(storage, kStorageId, &config,
                                       kTetheringConfUpstreamTechProperty);
  if (valid && !FromProperties(config).has_value()) {
    valid = false;
  }
  return valid && stable_mac_addr_.Load(storage, kStorageId);
}

// static
const char* TetheringManager::StopReasonToString(StopReason reason) {
  switch (reason) {
    case StopReason::kInitial:
      return kTetheringIdleReasonInitialState;
    case StopReason::kClientStop:
      return kTetheringIdleReasonClientStop;
    case StopReason::kUserExit:
      return kTetheringIdleReasonUserExit;
    case StopReason::kSuspend:
      return kTetheringIdleReasonSuspend;
    case StopReason::kUpstreamNotAvailable:
      return kTetheringIdleReasonUpstreamNotAvailable;
    case StopReason::kUpstreamDisconnect:
      return kTetheringIdleReasonUpstreamDisconnect;
    case StopReason::kUpstreamNoInternet:
      return kTetheringIdleReasonUpstreamNoInternet;
    case StopReason::kInactive:
      return kTetheringIdleReasonInactive;
    case StopReason::kError:
      return kTetheringIdleReasonError;
    case StopReason::kConfigChange:
      return kTetheringIdleReasonConfigChange;
    case StopReason::kDownstreamLinkDisconnect:
      return kTetheringIdleReasonDownstreamLinkDisconnect;
    case StopReason::kDownstreamNetDisconnect:
      return kTetheringIdleReasonDownstreamNetworkDisconnect;
    case StopReason::kStartTimeout:
      return kTetheringIdleReasonStartTimeout;
    case StopReason::kResourceBusy:
      return kTetheringIdleReasonResourceBusy;
    default:
      NOTREACHED() << "Unhandled stop reason " << static_cast<int>(reason);
      return "Invalid";
  }
}

void TetheringManager::HelpRegisterDerivedBool(
    PropertyStore* store,
    std::string_view name,
    bool (TetheringManager::*get)(Error* error),
    bool (TetheringManager::*set)(const bool&, Error*)) {
  store->RegisterDerivedBool(
      name,
      BoolAccessor(new CustomAccessor<TetheringManager, bool>(this, get, set)));
}

bool TetheringManager::SetAllowed(const bool& value, Error* error) {
  if (allowed_ == value)
    return false;

  LOG(INFO) << __func__ << " Allowed set to " << std::boolalpha << value;
  allowed_ = value;
  return true;
}

bool TetheringManager::SetExperimentalTetheringFunctionality(const bool& value,
                                                             Error* error) {
  if (experimental_tethering_functionality_ == value)
    return false;

  LOG(INFO) << __func__ << " set to " << std::boolalpha << value;
  experimental_tethering_functionality_ = value;
  RefreshCapabilities();
  return true;
}

void TetheringManager::OnNetworkValidationResult(
    int interface_index, const NetworkMonitor::Result& result) {
  DCHECK(upstream_network_);
  if (IsUpstreamNetworkReady()) {
    StopUpstreamNetworkValidationTimer();
  } else {
    StartUpstreamNetworkValidationTimer();
  }
}

void TetheringManager::OnNetworkStopped(int interface_index, bool is_failure) {
  if (state_ == TetheringState::kTetheringIdle ||
      state_ == TetheringState::kTetheringRestarting) {
    return;
  }
  StopTetheringSession(StopReason::kUpstreamDisconnect);
}

void TetheringManager::OnNetworkDestroyed(int network_id, int interface_index) {
  if (state_ == TetheringState::kTetheringIdle ||
      state_ == TetheringState::kTetheringRestarting) {
    return;
  }
  upstream_network_ = nullptr;
  upstream_service_ = nullptr;
  StopTetheringSession(StopReason::kUpstreamDisconnect);
}

bool TetheringManager::IsUpstreamNetworkReady() {
  if (!upstream_network_ || !upstream_network_->IsConnected()) {
    // Upstream network was not yet acquired or is not connected;
    return false;
  }
  const auto validation_result = upstream_network_->network_validation_result();
  if (!validation_result) {
    // Internet connectivity has not yet been evaluated.
    return false;
  }
  switch (validation_result->validation_state) {
    case PortalDetector::ValidationState::kInternetConnectivity:
      return true;
    case PortalDetector::ValidationState::kPortalRedirect:
      if (upstream_network_->technology() == Technology::kCellular) {
        // b/301648519: Some Cellular carriers use portal redirection flows for
        // asking the user to enable or buy a tethering data plan. This flow is
        // not handled natively in ChromeOS, but the network is nonetheless
        // considered ready.
        return true;
      }
      return false;
    case PortalDetector::ValidationState::kNoConnectivity:
      return false;
    case PortalDetector::ValidationState::kPortalSuspected:
      return false;
  }
}

void TetheringManager::OnNetworkValidationStop(int interface_index,
                                               bool is_failure) {
  // If network validation fails on the upstream network, do no wait for
  // the |upstream_network_validation_timer_callback_| to fire and terminate
  // the session immediately.
  if (is_failure) {
    StopTetheringSession(StopReason::kUpstreamNoInternet);
  }
}

void TetheringManager::OnNetworkValidationStart(int interface_index,
                                                bool is_failure) {
  // If network validation fails on the upstream network, do no wait for
  // the |upstream_network_validation_timer_callback_| to fire and terminate
  // the session immediately.
  if (is_failure) {
    StopTetheringSession(StopReason::kUpstreamNoInternet);
  }
}

}  // namespace shill
