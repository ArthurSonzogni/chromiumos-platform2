// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_MODEM_FLASHER_H_
#define MODEMFWD_MODEM_FLASHER_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>
#include <brillo/errors/error.h>
#include <chromeos/switches/modemfwd_switches.h>

#include "modemfwd/firmware_directory.h"
#include "modemfwd/firmware_file.h"
#include "modemfwd/modem.h"
#include "modemfwd/prefs.h"

namespace modemfwd {

struct FlashConfig {
  std::string carrier_id;
  std::vector<FirmwareConfig> fw_configs;
  std::map<std::string, std::unique_ptr<FirmwareFile>> files;
};

// ModemFlasher contains all of the logic to make decisions about whether
// or not it should flash new firmware onto the modem. Users can check if
// a modem has been blocked, and if they would like to proceed to flashing,
// fetch a list of firmware files they should flash, and then send those
// to the helper.
class ModemFlasher {
 public:
  virtual ~ModemFlasher() = default;

  virtual bool ShouldFlash(Modem* modem, brillo::ErrorPtr* err) = 0;
  virtual std::unique_ptr<FlashConfig> BuildFlashConfig(
      Modem* modem,
      std::optional<std::string> carrier_override_uuid,
      brillo::ErrorPtr* err) = 0;
  virtual bool RunFlash(Modem* modem,
                        const FlashConfig& flash_cfg,
                        base::TimeDelta* out_duration,
                        brillo::ErrorPtr* err) = 0;
};

std::unique_ptr<ModemFlasher> CreateModemFlasher(
    FirmwareDirectory* firmware_directory, Prefs* modems_seen_since_oobe_prefs);

}  // namespace modemfwd

#endif  // MODEMFWD_MODEM_FLASHER_H_
