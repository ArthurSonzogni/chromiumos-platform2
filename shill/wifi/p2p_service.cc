// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/p2p_service.h"

#include <vector>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/shill/dbus-constants.h>

#include "shill/store/property_accessor.h"
#include "shill/supplicant/wpa_supplicant.h"
#include "shill/wifi/local_device.h"

namespace shill {

P2PService::P2PService(LocalDeviceConstRefPtr device,
                       std::optional<std::string> ssid,
                       std::optional<std::string> passphrase,
                       std::optional<uint32_t> frequency)
    : LocalService(device),
      hex_ssid_(ssid),
      passphrase_(passphrase),
      security_(WiFiSecurity::kWpa2),
      frequency_(frequency) {}

KeyValueStore P2PService::GetSupplicantConfigurationParameters() const {
  KeyValueStore params;
  return params;
}

}  // namespace shill
