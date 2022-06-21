// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <set>
#include <vector>

#include "shill/tethering_manager.h"

#include <chromeos/dbus/shill/dbus-constants.h>

#include "shill/error.h"
#include "shill/store/property_accessor.h"

namespace shill {

TetheringManager::TetheringManager()
    : allowed_(false), state_(TetheringState::kTetheringIdle) {}

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
  return capabilities_;
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
  // Initialize tethering capabilities
  capabilities_.Set<Strings>(kTetheringCapUpstreamProperty, {});
  capabilities_.Set<Strings>(kTetheringCapDownstreamProperty, {});
  capabilities_.Set<Strings>(kTetheringCapSecurityProperty, {});
  capabilities_.Set<Strings>(kTetheringCapBandProperty, {});
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

}  // namespace shill
