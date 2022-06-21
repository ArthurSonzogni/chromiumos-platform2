// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_TETHERING_MANAGER_H_
#define SHILL_TETHERING_MANAGER_H_

#include "shill/store/property_store.h"

namespace shill {

// TetheringManager handles tethering related logics. It is created by the
// Manager class. It reuses the Profile class to persist the tethering
// parameters for each user. It interacts with HotspotDevice,
// CellularServiceProvider and EthernetProvider classes to prepare upstream and
// downstream technologies. It interacts with patchpanel via dbus to set up the
// tethering network.
class TetheringManager {
 public:
  TetheringManager();
  TetheringManager(const TetheringManager&) = delete;
  TetheringManager& operator=(const TetheringManager&) = delete;

  virtual ~TetheringManager();

  // Initialize DBus properties related to tethering.
  void InitPropertyStore(PropertyStore* store);
  // Start and initialize TetheringManager.
  void Start();
  // Stop TetheringManager.
  void Stop();
  // Enable or disable a tethering session with existing tethering config_.
  bool SetEnabled(bool enabled, Error* error);

 private:
  enum class TetheringState {
    kTetheringIdle,
    kTetheringStarting,
    kTetheringActive,
    kTetheringStopping,
    kTetheringFailure
  };

  KeyValueStore GetConfig(Error* error);
  bool SetConfig(const KeyValueStore& config, Error* error);
  KeyValueStore GetCapabilities(Error* error);
  KeyValueStore GetStatus(Error* error);
  static const char* TetheringStateToString(const TetheringState& state);

  // Tethering feature flag.
  bool allowed_;
  // Tethering state as listed in enum TetheringState.
  TetheringState state_;
  // Tethering capabilities including supported upstream and downstream
  // technologies, downstream WiFi supported bands and security modes.
  KeyValueStore capabilities_;
  // Tethering config including auto_disable, WiFi downstream SSID, band,
  // security, and passphrase.
  KeyValueStore config_;
};

}  // namespace shill

#endif  // SHILL_TETHERING_MANAGER_H_
