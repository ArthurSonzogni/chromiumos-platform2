// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/tethering_manager.h"

#include <math.h>
#include <stdint.h>

#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>

#include <base/rand_util.h>
#include <base/strings/string_number_conversions.h>
#include <chromeos/dbus/shill/dbus-constants.h>

#include "shill/cellular/cellular_service_provider.h"
#include "shill/error.h"
#include "shill/manager.h"
#include "shill/profile.h"
#include "shill/store/property_accessor.h"
#include "shill/technology.h"

namespace shill {

namespace {
static constexpr char kSSIDPrefix[] = "chromeOS-";
// Random suffix should provide enough uniqueness to have low SSID collision
// possibility, while have enough anonymity among chromeOS population to make
// the device untrackable. Use 4 digit numbers as random SSID suffix.
static constexpr size_t kSSIDSuffixLength = 4;
static constexpr size_t kMinWiFiPassphraseLength = 8;
static constexpr size_t kMaxWiFiPassphraseLength = 63;
// Auto disable tethering if no clients for |kAutoDisableMinute| minutes.
// static constexpr uint8_t kAutoDisableMinute = 10;

static const std::unordered_map<std::string, TetheringManager::WiFiBand>
    bands_map = {
        {kBand2GHz, TetheringManager::WiFiBand::kLowBand},
        {kBand5GHz, TetheringManager::WiFiBand::kHighBand},
        {kBand6GHz, TetheringManager::WiFiBand::kUltraHighBand},
        {kBandAll, TetheringManager::WiFiBand::kAllBands},
};
std::string BandToString(const TetheringManager::WiFiBand band) {
  auto it = std::find_if(std::begin(bands_map), std::end(bands_map),
                         [&band](auto& p) { return p.second == band; });
  return it == bands_map.end() ? std::string() : it->first;
}
TetheringManager::WiFiBand StringToBand(const std::string& band) {
  return base::Contains(bands_map, band)
             ? bands_map.at(band)
             : TetheringManager::WiFiBand::kInvalidBand;
}
std::ostream& operator<<(std::ostream& stream,
                         TetheringManager::WiFiBand band) {
  return stream << BandToString(band);
}

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

}  // namespace

TetheringManager::TetheringManager(Manager* manager)
    : manager_(manager),
      allowed_(false),
      state_(TetheringState::kTetheringIdle) {
  ResetConfiguration();
}

TetheringManager::~TetheringManager() = default;

void TetheringManager::ResetConfiguration() {
  auto_disable_ = true;
  upstream_technology_ = Technology::kUnknown;
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
  band_ = WiFiBand::kAllBands;
}

void TetheringManager::InitPropertyStore(PropertyStore* store) {
  store->RegisterBool(kTetheringAllowedProperty, &allowed_);
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
  if (band_ != WiFiBand::kAllBands && band_ != WiFiBand::kInvalidBand) {
    properties->Set<std::string>(kTetheringConfBandProperty,
                                 BandToString(band_));
  }
  if (upstream_technology_ != Technology::kUnknown) {
    properties->Set<std::string>(kTetheringConfUpstreamTechProperty,
                                 TechnologyName(upstream_technology_));
  }

  return true;
}

bool TetheringManager::FromProperties(const KeyValueStore& properties) {
  // sanity check.
  std::string ssid;
  if (properties.Contains<std::string>(kTetheringConfSSIDProperty)) {
    ssid = properties.Get<std::string>(kTetheringConfSSIDProperty);
    if (ssid.empty() || !std::all_of(ssid.begin(), ssid.end(), ::isxdigit)) {
      LOG(ERROR) << "Invalid SSID provided in tethering config: " << ssid;
      return false;
    }
  }

  std::string passphrase;
  if (properties.Contains<std::string>(kTetheringConfPassphraseProperty)) {
    passphrase = properties.Get<std::string>(kTetheringConfPassphraseProperty);
    if (passphrase.length() < kMinWiFiPassphraseLength ||
        passphrase.length() > kMaxWiFiPassphraseLength) {
      LOG(ERROR)
          << "Passphrase provided in tethering config has invalid length: "
          << passphrase;
      return false;
    }
  }

  auto sec = WiFiSecurity(WiFiSecurity::kNone);
  if (properties.Contains<std::string>(kTetheringConfSecurityProperty)) {
    sec = WiFiSecurity(
        properties.Get<std::string>(kTetheringConfSecurityProperty));
    if (!sec.IsValid() || !(sec == WiFiSecurity(WiFiSecurity::kWpa2) ||
                            sec == WiFiSecurity(WiFiSecurity::kWpa3) ||
                            sec == WiFiSecurity(WiFiSecurity::kWpa2Wpa3))) {
      LOG(ERROR) << "Invalid security mode provided in tethering config: "
                 << sec;
      return false;
    }
  }

  auto band = WiFiBand::kInvalidBand;
  if (properties.Contains<std::string>(kTetheringConfBandProperty)) {
    band =
        StringToBand(properties.Get<std::string>(kTetheringConfBandProperty));
    if (band == WiFiBand::kInvalidBand) {
      LOG(ERROR) << "Invalid WiFi band: " << band;
      return false;
    }
  }

  auto tech = Technology::kUnknown;
  if (properties.Contains<std::string>(kTetheringConfUpstreamTechProperty)) {
    tech = TechnologyFromName(
        properties.Get<std::string>(kTetheringConfUpstreamTechProperty));
    // TODO(b/235762746) Add support for WiFi as an upstream technology.
    if (tech != Technology::kUnknown && tech != Technology::kEthernet &&
        tech != Technology::kCellular) {
      LOG(ERROR) << "Invalid upstream technology provided in tethering config: "
                 << tech;
      return false;
    }
  }

  // update tethering config in this.
  if (properties.Contains<bool>(kTetheringConfAutoDisableProperty))
    auto_disable_ = properties.Get<bool>(kTetheringConfAutoDisableProperty);

  if (properties.Contains<bool>(kTetheringConfMARProperty))
    mar_ = properties.Get<bool>(kTetheringConfMARProperty);

  if (properties.Contains<std::string>(kTetheringConfSSIDProperty))
    hex_ssid_ = ssid;

  if (properties.Contains<std::string>(kTetheringConfPassphraseProperty))
    passphrase_ = passphrase;

  if (properties.Contains<std::string>(kTetheringConfSecurityProperty))
    security_ = sec;

  if (properties.Contains<std::string>(kTetheringConfBandProperty))
    band_ = band;

  if (properties.Contains<std::string>(kTetheringConfUpstreamTechProperty))
    upstream_technology_ = tech;

  return true;
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
  if (!allowed_) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kPermissionDenied,
                          "Tethering is not allowed");
    return false;
  }

  const auto profile = manager_->ActiveProfile();
  // TODO(b/172224298): prefer using Profile::IsDefault.
  if (profile->GetUser().empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kIllegalOperation,
                          "Tethering is not allowed without user profile");
    return false;
  }

  if (!FromProperties(config)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "Invalid tethering configuration");
    return false;
  }

  if (!Save(profile->GetStorage())) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                          "Failed to save config to user profile");
    return false;
  }
  return true;
}

KeyValueStore TetheringManager::GetCapabilities(Error* /* error */) {
  KeyValueStore caps;
  std::vector<std::string> upstream_technologies;
  std::vector<std::string> downstream_technologies;

  // Ethernet is always supported as an upstream technology.
  upstream_technologies.push_back(TechnologyName(Technology::kEthernet));

  // TODO(b/244334719): add a check with the CellularProvider to see if
  // tethering is enabled for the given SIM card and modem. For now assume
  // that Cellular is available as an upstream technology.
  upstream_technologies.push_back(TechnologyName(Technology::kCellular));

  // TODO(b/244335143): This should be based on static SoC capability
  // information. Need to revisit this when Shill has a SoC capability
  // database. For now, use the presence of a WiFi device as a proxy for
  // checking if AP mode is supported.
  const auto wifi_devices = manager_->FilterByTechnology(Technology::kWiFi);
  if (!wifi_devices.empty()) {
    WiFi* wifi_device = static_cast<WiFi*>(wifi_devices.front().get());
    if (wifi_device->SupportAP()) {
      downstream_technologies.push_back(TechnologyName(Technology::kWiFi));
      // Wi-Fi specific tethering capabilities.
      std::vector<std::string> security = {kSecurityWpa2};
      if (wifi_device->SupportsWPA3()) {
        security.push_back(kSecurityWpa3);
        security.push_back(kSecurityWpa2Wpa3);
      }
      caps.Set<Strings>(kTetheringCapSecurityProperty, security);
    }
  }

  caps.Set<Strings>(kTetheringCapUpstreamProperty, upstream_technologies);
  caps.Set<Strings>(kTetheringCapDownstreamProperty, downstream_technologies);

  return caps;
}

KeyValueStore TetheringManager::GetStatus(Error* /* error */) {
  KeyValueStore status;
  status.Set<std::string>(kTetheringStatusStateProperty,
                          TetheringStateToString(state_));
  return status;
}

// static
const char* TetheringManager::TetheringStateToString(
    const TetheringState& state) {
  switch (state) {
    case TetheringState::kTetheringIdle:
      return kTetheringStateIdle;
    case TetheringState::kTetheringStarting:
      return kTetheringStateStarting;
    case TetheringState::kTetheringActive:
      return kTetheringStateActive;
    case TetheringState::kTetheringStopping:
      return kTetheringStateStopping;
    case TetheringState::kTetheringFailure:
      return kTetheringStateFailure;
    default:
      NOTREACHED() << "Unhandled tethering state " << static_cast<int>(state);
      return "Invalid";
  }
}

void TetheringManager::Start() {}

void TetheringManager::Stop() {}

bool TetheringManager::SetEnabled(bool enabled, Error* error) {
  if (!allowed_) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kPermissionDenied,
                          "Tethering is not allowed");
    return false;
  }

  const auto profile = manager_->ActiveProfile();
  // TODO(b/172224298): prefer using Profile::IsDefault.
  if (profile->GetUser().empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kIllegalOperation,
                          "Tethering is not allowed without user profile");
    return false;
  }

  if (!Save(profile->GetStorage())) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                          "Failed to save config to user profile");
    return false;
  }

  // TODO(b/235762746) If the upstream technology is Cellular, obtain the
  // upstream Network with CellularServiceProvider::AcquireTetheringNetwork.

  // TODO(b/235762746) Check if Internet access has been validated on the
  // upstream network (pending integration of PortalDetection into the Network
  // class).

  // TODO(b/235762439): Routine to enable/disable tethering session.
  return true;
}

void TetheringManager::CheckReadiness(
    base::OnceCallback<void(EntitlementStatus result)> callback) {
  if (!allowed_) {
    LOG(ERROR) << __func__ << ": not allowed";
    manager_->dispatcher()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback), EntitlementStatus::kNotAllowed));
    return;
  }

  // TODO(b/235762746) Add a selection mode for choosing the current default
  // network as the upstream network.

  Technology upstream_technology = upstream_technology_;
  // Select Cellular for the upstream network by default if the upstream
  // technology has not been specified.
  if (upstream_technology == Technology::kUnknown) {
    upstream_technology = Technology::kCellular;
  }

  // Validate the upstream technology.
  switch (upstream_technology) {
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

  // TODO(b/235762746) Check if Internet access has been validated.

  // When the upstream technology is Cellular, delegate to the Provider.
  if (upstream_technology_ == Technology::kCellular) {
    manager_->cellular_service_provider()->TetheringEntitlementCheck(
        std::move(callback));
    return;
  }

  // Otherwise for WiFi or Ethernet, there is no other entitlement check.
  manager_->dispatcher()->PostTask(
      FROM_HERE,
      base::BindOnce(std::move(callback), EntitlementStatus::kReady));
}

// static
const char* TetheringManager::EntitlementStatusName(EntitlementStatus status) {
  switch (status) {
    case EntitlementStatus::kReady:
      return kTetheringReadinessReady;
    case EntitlementStatus::kNotAllowed:
      return kTetheringReadinessNotAllowed;
    case EntitlementStatus::kUpstreamNetworkNotAvailable:
      return kTetheringReadinessUpstreamNetworkNotAvailable;
    default:
      return "unknown";
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
}

void TetheringManager::UnloadConfigFromProfile() {
  ResetConfiguration();
}

bool TetheringManager::Save(StoreInterface* storage) {
  storage->SetBool(kStorageId, kTetheringConfAutoDisableProperty,
                   auto_disable_);
  storage->SetBool(kStorageId, kTetheringConfMARProperty, mar_);
  storage->SetString(kStorageId, kTetheringConfSSIDProperty, hex_ssid_);
  storage->SetString(kStorageId, kTetheringConfPassphraseProperty, passphrase_);
  storage->SetString(kStorageId, kTetheringConfSecurityProperty,
                     security_.ToString());
  storage->SetString(kStorageId, kTetheringConfBandProperty,
                     BandToString(band_));
  storage->SetString(kStorageId, kTetheringConfUpstreamTechProperty,
                     TechnologyName(upstream_technology_));
  return storage->Flush();
}

bool TetheringManager::Load(const StoreInterface* storage) {
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
  if (valid && !FromProperties(config)) {
    valid = false;
  }
  return valid;
}

}  // namespace shill
