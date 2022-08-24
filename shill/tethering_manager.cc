// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <set>
#include <vector>

#include "shill/tethering_manager.h"

#include <chromeos/dbus/shill/dbus-constants.h>

#include "shill/error.h"
#include "shill/manager.h"
#include "shill/store/property_accessor.h"
#include "shill/technology.h"

namespace shill {

TetheringManager::TetheringManager(Manager* manager)
    : manager_(manager),
      allowed_(false),
      state_(TetheringState::kTetheringIdle) {}

TetheringManager::~TetheringManager() = default;

void TetheringManager::InitPropertyStore(PropertyStore* store) {
  store->RegisterBool(kTetheringAllowedProperty, &allowed_);
  store->RegisterDerivedKeyValueStore(
      kTetheringConfigProperty,
      KeyValueStoreAccessor(new CustomAccessor<TetheringManager, KeyValueStore>(
          this, &TetheringManager::GetConfig, &TetheringManager::SetConfig)));
  store->RegisterDerivedKeyValueStore(
      kTetheringCapabilitiesProperty,
      KeyValueStoreAccessor(new CustomAccessor<TetheringManager, KeyValueStore>(
          this, &TetheringManager::GetCapabilities, nullptr)));
  store->RegisterDerivedKeyValueStore(
      kTetheringStatusProperty,
      KeyValueStoreAccessor(new CustomAccessor<TetheringManager, KeyValueStore>(
          this, &TetheringManager::GetStatus, nullptr)));
}

KeyValueStore TetheringManager::GetConfig(Error* /*error*/) {
  return config_;
}

bool TetheringManager::SetConfig(const KeyValueStore& config, Error* error) {
  if (!allowed_) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kPermissionDenied,
                          "Tethering is not allowed");
    return false;
  }

  return true;
}

KeyValueStore TetheringManager::GetCapabilities(Error* /* error */) {
  KeyValueStore caps;
  std::vector<std::string> upstream_technologies;
  std::vector<std::string> downstream_technologies;

  if (manager_->GetProviderWithTechnology(Technology::kEthernet))
    upstream_technologies.push_back(TechnologyName(Technology::kEthernet));

  // TODO(b/244334719): add a check with the CellularProvider to see if
  // tethering is enabled for the given SIM card and modem.
  if (manager_->GetProviderWithTechnology(Technology::kCellular))
    upstream_technologies.push_back(TechnologyName(Technology::kCellular));

  if (manager_->GetProviderWithTechnology(Technology::kWiFi)) {
    // TODO(b/244335143): This should be based on static SoC capability
    // information. Need to revisit this when Shill has a SoC capability
    // database.
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

void TetheringManager::Start() {
}

void TetheringManager::Stop() {}

bool TetheringManager::SetEnabled(bool enabled, Error* error) {
  if (!allowed_) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kPermissionDenied,
                          "Tethering is not allowed");
    return false;
  }

  return true;
}

std::string TetheringManager::TetheringManager::CheckReadiness(Error* error) {
  if (!allowed_) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kPermissionDenied,
                          "Tethering is not allowed");
    return kTetheringReadinessNotAllowed;
  }

  // TODO(b/235762746): Stub method handler always return "ready". Add more
  // status code later.
  return kTetheringReadinessReady;
}

}  // namespace shill
