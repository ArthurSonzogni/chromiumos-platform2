// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/tethering_manager.h"

#include <math.h>
#include <stdint.h>

#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/rand_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/time/time.h>
#include <chromeos/dbus/shill/dbus-constants.h>

#include "shill/cellular/cellular_service_provider.h"
#include "shill/device.h"
#include "shill/error.h"
#include "shill/manager.h"
#include "shill/profile.h"
#include "shill/store/property_accessor.h"
#include "shill/technology.h"
#include "shill/wifi/hotspot_device.h"
#include "shill/wifi/hotspot_service.h"
#include "shill/wifi/wifi.h"
#include "shill/wifi/wifi_phy.h"
#include "shill/wifi/wifi_provider.h"

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
// Auto disable tethering if no clients for |kAutoDisableDelay|.
static constexpr base::TimeDelta kAutoDisableDelay = base::Minutes(5);

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
      state_(TetheringState::kTetheringIdle),
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
  band_ = WiFiBand::kAllBands;
}

void TetheringManager::InitPropertyStore(PropertyStore* store) {
  HelpRegisterDerivedBool(store, kTetheringAllowedProperty,
                          &TetheringManager::GetAllowed,
                          &TetheringManager::SetAllowed);
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

  return true;
}

bool TetheringManager::FromProperties(const KeyValueStore& properties) {
  // sanity check.
  std::string ssid;
  if (properties.Contains<std::string>(kTetheringConfSSIDProperty)) {
    ssid = properties.Get<std::string>(kTetheringConfSSIDProperty);
    if (ssid.empty() || ssid.length() > kMaxWiFiHexSSIDLength ||
        !std::all_of(ssid.begin(), ssid.end(), ::isxdigit)) {
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

  auto band = WiFiBand::kUnknownBand;
  if (properties.Contains<std::string>(kTetheringConfBandProperty)) {
    band = WiFiBandFromName(
        properties.Get<std::string>(kTetheringConfBandProperty));
    if (band == WiFiBand::kUnknownBand) {
      LOG(ERROR) << "Invalid WiFi band: " << band;
      return false;
    }
  }

  auto tech = Technology::kUnknown;
  if (properties.Contains<std::string>(kTetheringConfUpstreamTechProperty)) {
    tech = TechnologyFromName(
        properties.Get<std::string>(kTetheringConfUpstreamTechProperty));
    // TODO(b/235762746) Add support for WiFi as an upstream technology.
    if (tech != Technology::kEthernet && tech != Technology::kCellular) {
      LOG(ERROR) << "Invalid upstream technology provided in tethering config: "
                 << tech;
      return false;
    }
  }

  // update tethering config in this.
  if (properties.Contains<bool>(kTetheringConfAutoDisableProperty) &&
      auto_disable_ !=
          properties.Get<bool>(kTetheringConfAutoDisableProperty)) {
    auto_disable_ = properties.Get<bool>(kTetheringConfAutoDisableProperty);
    auto_disable_ ? StartInactiveTimer() : StopInactiveTimer();
  }

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
  // database. For now, use the presence of a WiFi phy as a proxy for
  // checking if AP mode is supported.
  auto wifi_phys = manager_->wifi_provider()->GetPhys();
  if (!wifi_phys.empty()) {
    if (wifi_phys.front()->SupportAPMode()) {
      downstream_technologies.push_back(TechnologyName(Technology::kWiFi));
      // Wi-Fi specific tethering capabilities.
      // TODO(b/273351443) Add WPA2WPA3 and WPA3 security capability to
      // supported chipset.
      caps.Set<Strings>(kTetheringCapSecurityProperty, {kSecurityWpa2});
    }
  }

  caps.Set<Strings>(kTetheringCapUpstreamProperty, upstream_technologies);
  caps.Set<Strings>(kTetheringCapDownstreamProperty, downstream_technologies);

  return caps;
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
  auto stations = hotspot_dev_->GetStations();
  for (auto const& station : stations) {
    Stringmap client;
    client.insert({kTetheringStatusClientMACProperty,
                   Device::MakeStringFromHardwareAddress(station)});
    // TODO(b/235763170): Get IP address and hostname from patchpanel
    clients.push_back(client);
  }
  status.Set<Stringmaps>(kTetheringStatusClientsProperty, clients);

  return status;
}

size_t TetheringManager::GetClientCount() {
  return hotspot_dev_->GetStations().size();
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

void TetheringManager::CheckAndPostTetheringResult() {
  if (!hotspot_dev_ || !hotspot_dev_->IsServiceUp()) {
    // Downstream hotspot device or service is not ready.
    if (hotspot_service_up_) {
      // Has already received the kServiceUp event, but device state is not
      // correct, something went wrong. Terminate tethering session.
      LOG(ERROR) << "Has received kServiceUp event from hotspot device but the "
                    "device state is not correct. Terminate tethering session";
      PostSetEnabledResult(SetEnabledResult::kFailure);
      StopTetheringSession(StopReason::kError);
    }
    return;
  }

  // TODO(b/235762439): Check other tethering modules.

  // TODO(b/235762746) Check if Internet access has been validated on the
  // upstream network (pending integration of PortalDetection into the Network
  // class).

  SetState(TetheringState::kTetheringActive);
  if (hotspot_dev_->GetStations().size() == 0) {
    // Kick off inactive timer when tethering session becomes active and no
    // clients are connected.
    StartInactiveTimer();
  }
  PostSetEnabledResult(SetEnabledResult::kSuccess);
}

void TetheringManager::StartTetheringSession() {
  if (hotspot_dev_) {
    LOG(ERROR) << "Tethering resources are not null when starting tethering "
                  "session.";
    PostSetEnabledResult(SetEnabledResult::kFailure);
    return;
  }

  LOG(INFO) << __func__;

  // Prepare the downlink hotspot device.
  // TODO(b/235760422) Generate random MAC address and pass it to
  // WiFiProvider.
  hotspot_service_up_ = false;
  hotspot_dev_ = manager_->wifi_provider()->CreateHotspotDevice(
      "", band_, security_,
      base::BindRepeating(&TetheringManager::OnDownstreamDeviceEvent,
                          base::Unretained(this)));
  if (!hotspot_dev_) {
    LOG(ERROR) << __func__ << ": failed to create a WiFi AP interface";
    PostSetEnabledResult(SetEnabledResult::kFailure);
    StopTetheringSession(StopReason::kError);
    return;
  }

  // Prepare the downlink service.
  std::unique_ptr<HotspotService> service = std::make_unique<HotspotService>(
      hotspot_dev_, hex_ssid_, passphrase_, security_);
  if (!hotspot_dev_->ConfigureService(std::move(service))) {
    LOG(ERROR) << __func__ << ": failed to configure the service";
    PostSetEnabledResult(SetEnabledResult::kFailure);
    StopTetheringSession(StopReason::kError);
    return;
  }

  // TODO(b/235762439): Routine to enable other tethering modules.

  // TODO(b/235762439): Add timer to timeout the start process if resources are
  // not ready for a long time.

  // TODO(b/235762746) If the upstream technology is Cellular, obtain the
  // upstream Network with CellularServiceProvider::AcquireTetheringNetwork.
}

void TetheringManager::StopTetheringSession(StopReason reason) {
  if (state_ == TetheringState::kTetheringIdle) {
    return;
  }

  LOG(INFO) << __func__ << ": " << StopReasonToString(reason);
  stop_reason_ = reason;

  // Remove the downstream device if any.
  if (hotspot_dev_) {
    hotspot_dev_->DeconfigureService();
    manager_->wifi_provider()->DeleteLocalDevice(hotspot_dev_);
    hotspot_dev_ = nullptr;
  }

  hotspot_service_up_ = false;
  StopInactiveTimer();
  // TODO(b/235762439): Routine to disable other tethering modules.
  SetState(TetheringState::kTetheringIdle);
}

void TetheringManager::StartInactiveTimer() {
  if (!auto_disable_ || !inactive_timer_callback_.IsCancelled() ||
      state_ != TetheringState::kTetheringActive) {
    return;
  }

  LOG(INFO) << __func__ << ": timer fires in " << kAutoDisableDelay;

  inactive_timer_callback_.Reset(
      base::BindOnce(&TetheringManager::StopTetheringSession,
                     base::Unretained(this), StopReason::kInactive));
  manager_->dispatcher()->PostDelayedTask(
      FROM_HERE, inactive_timer_callback_.callback(), kAutoDisableDelay);
}

void TetheringManager::StopInactiveTimer() {
  if (inactive_timer_callback_.IsCancelled()) {
    return;
  }

  inactive_timer_callback_.Cancel();
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
               << device->link_name();
    return;
  }

  LOG(INFO) << "TetheringManager receive downstream device "
            << device->link_name() << " event: " << event;

  if (event == LocalDevice::DeviceEvent::kInterfaceDisabled ||
      event == LocalDevice::DeviceEvent::kServiceDown) {
    if (state_ == TetheringState::kTetheringStarting) {
      PostSetEnabledResult(SetEnabledResult::kFailure);
    }
    StopTetheringSession(StopReason::kError);
  } else if (event == LocalDevice::DeviceEvent::kServiceUp) {
    hotspot_service_up_ = true;
    CheckAndPostTetheringResult();
  } else if (event == LocalDevice::DeviceEvent::kPeerConnected) {
    OnPeerAssoc();
  } else if (event == LocalDevice::DeviceEvent::kPeerDisconnected) {
    OnPeerDisassoc();
  }
}

void TetheringManager::SetEnabled(bool enabled,
                                  SetEnabledResultCallback callback) {
  result_callback_ = std::move(callback);

  if (!allowed_) {
    LOG(ERROR) << __func__ << ": not allowed";
    PostSetEnabledResult(SetEnabledResult::kNotAllowed);
    return;
  }

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

  if (enabled) {
    if (state_ != TetheringState::kTetheringIdle) {
      LOG(ERROR) << __func__ << ": tethering session is not in idle state";
      PostSetEnabledResult(SetEnabledResult::kFailure);
      return;
    }

    SetState(TetheringState::kTetheringStarting);
    StartTetheringSession();
  } else {
    if (state_ != TetheringState::kTetheringActive) {
      LOG(ERROR) << __func__ << ": no active tethering session";
      PostSetEnabledResult(SetEnabledResult::kFailure);
      return;
    }

    StopTetheringSession(StopReason::kClientStop);
    PostSetEnabledResult(SetEnabledResult::kSuccess);
  }
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
    case SetEnabledResult::kUpstreamNetworkNotAvailable:
      return kTetheringEnableResultUpstreamNotAvailable;
    default:
      return "unknown";
  }
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

  stop_reason_ = StopReason::kInitial;
}

void TetheringManager::UnloadConfigFromProfile() {
  StopTetheringSession(StopReason::kUserExit);
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
                     WiFiBandName(band_));
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
    case StopReason::kUpstreamDisconnect:
      return kTetheringIdleReasonUpstreamDisconnect;
    case StopReason::kInactive:
      return kTetheringIdleReasonInactive;
    case StopReason::kError:
      return kTetheringIdleReasonError;
    default:
      NOTREACHED() << "Unhandled stop reason " << static_cast<int>(reason);
      return "Invalid";
  }
}

void TetheringManager::HelpRegisterDerivedBool(
    PropertyStore* store,
    const std::string& name,
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
  manager_->dispatcher()->PostTask(
      FROM_HERE, base::BindRepeating(&TetheringManager::TetheringAllowedUpdated,
                                     weak_ptr_factory_.GetWeakPtr(), allowed_));

  return true;
}

void TetheringManager::TetheringAllowedUpdated(bool allowed) {
  const auto cellular_devices =
      manager_->FilterByTechnology(Technology::kCellular);
  for (auto device : cellular_devices) {
    Cellular* cellular_device = static_cast<Cellular*>(device.get());
    cellular_device->TetheringAllowedUpdated(allowed);
  }
}

}  // namespace shill
