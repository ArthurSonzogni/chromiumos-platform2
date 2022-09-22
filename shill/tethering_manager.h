// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_TETHERING_MANAGER_H_
#define SHILL_TETHERING_MANAGER_H_

#include <string>

#include "shill/store/property_store.h"
#include "shill/technology.h"
#include "shill/wifi/wifi_security.h"
#include "shill/wifi/wifi_service.h"

namespace shill {

class Manager;
class StoreInterface;

// TetheringManager handles tethering related logics. It is created by the
// Manager class.
//
// It reuses the Profile class to persist the tethering parameters for each
// user. Without user's input, it uses the default tethering configuration with
// a random SSID and a random passphrase. It saves the current tethering
// configuration to user profile when the user sets tethering config, or user
// enables tethering.
//
// It interacts with HotspotDevice,
// CellularServiceProvider and EthernetProvider classes to prepare upstream and
// downstream technologies. It interacts with patchpanel via dbus to set up the
// tethering network.
class TetheringManager {
 public:
  enum class EntitlementStatus {
    kReady,
    kNotAllowed,
  };

  static const char* EntitlementStatusName(EntitlementStatus status);

  enum class WiFiBand : uint8_t {
    kLowBand,
    kHighBand,
    kUltraHighBand,
    kAllBands,
    kInvalidBand,
  };

  // Storage group for tethering configs.
  static constexpr char kStorageId[] = "tethering";

  explicit TetheringManager(Manager* manager);
  TetheringManager(const TetheringManager&) = delete;
  TetheringManager& operator=(const TetheringManager&) = delete;

  virtual ~TetheringManager();

  // Initialize DBus properties related to tethering.
  void InitPropertyStore(PropertyStore* store);
  // Start and initialize TetheringManager.
  void Start();
  // Stop TetheringManager.
  void Stop();
  // Enable or disable a tethering session with existing tethering config.
  bool SetEnabled(bool enabled, Error* error);
  // Check if upstream network is ready for tethering.
  EntitlementStatus CheckReadiness(Error* error);
  // Load the tethering config available in |profile| if there was any tethering
  // config saved for this |profile|.
  virtual void LoadConfigFromProfile(const ProfileRefPtr& profile);
  // Unload the tethering config related to |profile| and reset the tethering
  // config with default values.
  virtual void UnloadConfigFromProfile();

 private:
  friend class TetheringManagerTest;
  FRIEND_TEST(TetheringManagerTest, FromProperties);
  FRIEND_TEST(TetheringManagerTest, GetCapabilities);
  FRIEND_TEST(TetheringManagerTest, GetConfig);
  FRIEND_TEST(TetheringManagerTest, GetTetheringCapabilities);
  FRIEND_TEST(TetheringManagerTest, SaveConfig);
  FRIEND_TEST(TetheringManagerTest, SetEnabled);

  enum class TetheringState {
    kTetheringIdle,
    kTetheringStarting,
    kTetheringActive,
    kTetheringStopping,
    kTetheringFailure
  };

  KeyValueStore GetCapabilities(Error* error);
  KeyValueStore GetConfig(Error* error);
  bool SetAndPersistConfig(const KeyValueStore& config, Error* error);
  KeyValueStore GetStatus(Error* error);
  static const char* TetheringStateToString(const TetheringState& state);
  // Populate the shill D-Bus parameter map |properties| with the
  // parameters contained in |this| and return true if successful.
  bool ToProperties(KeyValueStore* properties) const;
  // Populate tethering config from a dictionary.
  bool FromProperties(const KeyValueStore& properties);
  // Reset tethering config with default value and a random WiFi SSID and
  // a random passphrase.
  void ResetConfiguration();
  // Save the current tethering config to user's profile.
  bool Save(StoreInterface* storage);
  // Load the current tethering config from user's profile.
  bool Load(const StoreInterface* storage);

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
