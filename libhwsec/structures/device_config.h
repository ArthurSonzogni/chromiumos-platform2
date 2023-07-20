// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_STRUCTURES_DEVICE_CONFIG_H_
#define LIBHWSEC_STRUCTURES_DEVICE_CONFIG_H_

#include <bitset>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

#include <absl/container/flat_hash_map.h>

#include "libhwsec/structures/no_default_init.h"

namespace hwsec {

enum class DeviceConfig {
  kBootMode,
  kDeviceModel,
  kCurrentUser,
  kBootCmdline,
};

constexpr size_t kDeviceConfigArraySize = 4;

static_assert(static_cast<size_t>(DeviceConfig::kBootMode) <
              kDeviceConfigArraySize);
static_assert(static_cast<size_t>(DeviceConfig::kDeviceModel) <
              kDeviceConfigArraySize);
static_assert(static_cast<size_t>(DeviceConfig::kCurrentUser) <
              kDeviceConfigArraySize);
static_assert(static_cast<size_t>(DeviceConfig::kBootCmdline) <
              kDeviceConfigArraySize);

class DeviceConfigs : public std::bitset<kDeviceConfigArraySize> {
 public:
  DeviceConfigs() = default;
  explicit DeviceConfigs(std::initializer_list<DeviceConfig> init) {
    for (DeviceConfig config : init) {
      operator[](config) = true;
    }
  }

  bool operator[](DeviceConfig pos) const {
    return bitset::operator[](static_cast<size_t>(pos));
  }
  reference operator[](DeviceConfig pos) {
    return bitset::operator[](static_cast<size_t>(pos));
  }
};

enum class EndorsementAuth : bool {
  kNoEndorsement = false,
  kEndorsement = true,
};

struct DeviceConfigSettings {
  struct BootModeSetting {
    struct Mode {
      NoDefault<bool> developer_mode;
      NoDefault<bool> recovery_mode;
      NoDefault<bool> verified_firmware;
    };

    // std::nullopt means using current setting.
    std::optional<Mode> mode;
  };

  struct DeviceModelSetting {
    // std::nullopt means using current setting.
    std::optional<std::string> hardware_id;
  };

  struct CurrentUserSetting {
    // std::nullopt means prior login state.
    std::optional<std::string> username;
  };

  // std::nullopt means ignoring the setting.
  std::optional<BootModeSetting> boot_mode;
  std::optional<DeviceModelSetting> device_model;
  std::optional<CurrentUserSetting> current_user;

  // Uses endorsement password to authorize.
  bool use_endorsement_auth = false;
};

}  // namespace hwsec

#endif  // LIBHWSEC_STRUCTURES_DEVICE_CONFIG_H_
