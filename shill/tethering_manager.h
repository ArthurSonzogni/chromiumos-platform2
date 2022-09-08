// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_TETHERING_MANAGER_H_
#define SHILL_TETHERING_MANAGER_H_

#include "shill/store/property_store.h"
#include "shill/technology.h"
#include "shill/wifi/wifi_security.h"
#include "shill/wifi/wifi_service.h"

namespace shill {

class Manager;

// TetheringManager handles tethering related logics. It is created by the
// Manager class. It reuses the Profile class to persist the tethering
// parameters for each user. It interacts with HotspotDevice,
// CellularServiceProvider and EthernetProvider classes to prepare upstream and
// downstream technologies. It interacts with patchpanel via dbus to set up the
// tethering network.
class TetheringManager {
 public:
  enum class WiFiBand : uint8_t {
    kLowBand,
    kHighBand,
    kUltraHighBand,
    kAllBands,
    kInvalidBand,
  };

  explicit TetheringManager(Manager* manager);
  TetheringManager(const TetheringManager&) = delete;
  TetheringManager& operator=(const TetheringManager&) = delete;

  ~TetheringManager();

  // Initialize DBus properties related to tethering.
  void InitPropertyStore(PropertyStore* store);
  // Start and initialize TetheringManager.
  void Start();
  // Stop TetheringManager.
  void Stop();
  // Enable or disable a tethering session with existing tethering config.
  bool SetEnabled(bool enabled, Error* error);
  // Check if upstream network is ready for tethering.
  std::string CheckReadiness(Error* error);

 private:
  friend class TetheringManagerTest;
  FRIEND_TEST(TetheringManagerTest, GetTetheringCapabilities);
  FRIEND_TEST(TetheringManagerTest, TetheringConfig);

  enum class TetheringState {
    kTetheringIdle,
    kTetheringStarting,
    kTetheringActive,
    kTetheringStopping,
    kTetheringFailure
  };

  KeyValueStore GetCapabilities(Error* error);
  KeyValueStore GetConfig(Error* error);
  bool SetConfig(const KeyValueStore& config, Error* error);
  KeyValueStore GetStatus(Error* error);
  static const char* TetheringStateToString(const TetheringState& state);
  // Populate the shill D-Bus parameter map |properties| with the
  // parameters contained in |this| and return true if successful.
  bool ToProperties(KeyValueStore* properties) const;
  // Populate tethering config from a dictionary.
  bool FromProperties(const KeyValueStore& properties);
  // Generate random WiFi SSID and passphrase.
  void GenerateRandomWiFiProfile();

  // TetheringManager is created and owned by Manager.
  Manager* manager_;
  // Tethering feature flag.
  bool allowed_;
  // Tethering state as listed in enum TetheringState.
  TetheringState state_;

  // Automatically disable tethering if no devices have been associated for
  // |kAutoDisableMinute| minutes.
  bool auto_disable_;
  // MAC address randomization. When it is true, AP will use a randomized MAC
  // each time it is started. If false, it will use the persisted MAC address.
  bool mar_;
  // The hex-encoded tethering SSID name to be used in WiFi downstream.
  std::string hex_ssid_;
  // The passphrase to be used in WiFi downstream.
  std::string passphrase_;
  // The security mode to be used in WiFi downstream.
  WiFiSecurity security_;
  // The preferred band to be used in WiFi downstream.
  WiFiBand band_;
  // Preferred upstream technology to use.
  Technology upstream_technology_;
};

}  // namespace shill

#endif  // SHILL_TETHERING_MANAGER_H_
