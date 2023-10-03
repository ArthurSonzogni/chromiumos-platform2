// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_P2P_SERVICE_H_
#define SHILL_WIFI_P2P_SERVICE_H_

#include <optional>
#include <string>

#include "shill/refptr_types.h"
#include "shill/wifi/local_service.h"
#include "shill/wifi/wifi_security.h"

namespace shill {

class LocalDevice;
class Manager;

// A P2PService inherits from the base class LocalService and represents a
// wpa_supplicant network in mode P2P.
class P2PService : public LocalService {
 public:
  P2PService(LocalDeviceConstRefPtr device,
             std::optional<std::string> ssid,
             std::optional<std::string> passphrase,
             std::optional<uint32_t> frequency);
  P2PService(const P2PService&) = delete;
  P2PService& operator=(const P2PService&) = delete;

  // Stub function returns an empty KeyValueStore.
  KeyValueStore GetSupplicantConfigurationParameters() const override;

  std::optional<uint32_t> frequency() { return frequency_; }

 private:
  // The hex-encoded tethering SSID name to be used in WiFi P2P. No value means
  // randomly generate a SSID with Direct- prefix.
  std::optional<std::string> hex_ssid_;

  // The passphrase to be used in WiFi P2P. no value means randomly generate
  // a 8 bytes passphrase.
  std::optional<std::string> passphrase_;

  // The security mode to be used in WiFi P2P. Currently only wpa2 is
  // supported for P2P.
  WiFiSecurity security_;

  // The WiFi P2P frequency. No value indicates that frequency should be chosen
  // by supplicant.
  std::optional<uint32_t> frequency_;
};

}  // namespace shill

#endif  // SHILL_WIFI_P2P_SERVICE_H_
