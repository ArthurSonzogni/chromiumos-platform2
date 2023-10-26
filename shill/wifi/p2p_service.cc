// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/p2p_service.h"

#include <vector>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/shill/dbus-constants.h>

#include "shill/logging.h"
#include "shill/store/property_accessor.h"
#include "shill/supplicant/wpa_supplicant.h"
#include "shill/wifi/local_device.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kWiFi;
}  // namespace Logging

P2PService::P2PService(LocalDeviceConstRefPtr device,
                       std::optional<std::string> ssid,
                       std::optional<std::string> passphrase,
                       std::optional<uint32_t> frequency)
    : LocalService(device),
      hex_ssid_(ssid),
      passphrase_(passphrase),
      security_(WiFiSecurity::kWpa2),
      frequency_(frequency) {}

KeyValueStore P2PService::GetSupplicantGOConfigurationParameters() const {
  KeyValueStore params;
  if (hex_ssid_.has_value()) {
    SLOG(2) << __func__ << ": ssid: " << hex_ssid_.value();
    // TODO(b/295053632): current implementation of wpa_supplicant
    // does not support custom ssid in GroupAdd method.
    // params.Set<String>(WPASupplicant::kGroupAddPropertySSID,
    //                    hex_ssid_.value());
  }
  if (passphrase_.has_value()) {
    SLOG(2) << __func__ << ": passphrase: " << passphrase_.value();
    // TODO(b/295053632): current implementation of wpa_supplicant
    // does not support custom passphrase in GroupAdd method.
    // params.Set<String>(WPASupplicant::kGroupAddPropertyPassphrase,
    //                    passphrase_.value());
  }
  if (frequency_.has_value()) {
    SLOG(2) << __func__
            << ": frequency: " << std::to_string(frequency_.value());
    params.Set<Integer>(WPASupplicant::kGroupAddPropertyFrequency,
                        frequency_.value());
  }
  params.Set<Boolean>(WPASupplicant::kGroupAddPropertyPersistent, false);
  return params;
}

KeyValueStore P2PService::GetSupplicantClientConfigurationParameters() const {
  KeyValueStore params;
  if (hex_ssid_.has_value()) {
    SLOG(2) << __func__ << ": ssid: " << hex_ssid_.value();
    params.Set<String>(WPASupplicant::kAddPersistentGroupPropertySSID,
                       hex_ssid_.value());
  }
  if (passphrase_.has_value()) {
    SLOG(2) << __func__ << ": passphrase: " << passphrase_.value();
    params.Set<String>(WPASupplicant::kAddPersistentGroupPropertyPassphrase,
                       passphrase_.value());
  }
  if (frequency_.has_value()) {
    SLOG(2) << __func__
            << ": frequency: " << std::to_string(frequency_.value());
    params.Set<Integer>(WPASupplicant::kAddPersistentGroupPropertyFrequency,
                        frequency_.value());
  }
  params.Set<Integer>(WPASupplicant::kAddPersistentGroupPropertyMode,
                      WPASupplicant::kAddPersistentGroupModeClient);
  return params;
}

KeyValueStore P2PService::GetSupplicantConfigurationParameters() const {
  KeyValueStore params;
  switch (device()->iface_type()) {
    case LocalDevice::IfaceType::kP2PGO:
      params = GetSupplicantGOConfigurationParameters();
      break;
    case LocalDevice::IfaceType::kP2PClient:
      params = GetSupplicantClientConfigurationParameters();
      break;
    default:
      LOG(ERROR) << __func__
                 << ": Unexpected iface_type: " << device()->iface_type();
      break;
  }
  return params;
}

}  // namespace shill
