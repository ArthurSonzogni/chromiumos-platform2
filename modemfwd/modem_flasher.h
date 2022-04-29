// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_MODEM_FLASHER_H_
#define MODEMFWD_MODEM_FLASHER_H_

#include <map>
#include <memory>
#include <set>
#include <string>

#include <base/callback.h>

#include "modemfwd/firmware_directory.h"
#include "modemfwd/journal.h"
#include "modemfwd/modem.h"
#include "modemfwd/notification_manager.h"

namespace modemfwd {

// ModemFlasher contains all of the logic to make decisions about whether
// or not it should flash new firmware onto the modem.
class ModemFlasher {
 public:
  ModemFlasher(FirmwareDirectory* firmware_directory,
               std::unique_ptr<Journal> journal,
               NotificationManager* notification_mgr);
  ModemFlasher(const ModemFlasher&) = delete;
  ModemFlasher& operator=(const ModemFlasher&) = delete;

  // Returns a callback that should be executed when the modem reappears.
  base::OnceClosure TryFlash(Modem* modem);

  // This function is the same as TryFlash, but it sets the variant to be used.
  // Returns a callback that should be executed when the modem reappears.
  base::OnceClosure TryFlashForTesting(Modem* modem,
                                       const std::string& variant);

 private:
  class FlashState {
   public:
    FlashState() = default;
    ~FlashState() = default;

    void OnFlashFailed() { tries_--; }
    bool ShouldFlash() const { return tries_ > 0; }

    void OnFlashedMainFirmware() { should_flash_main_fw_ = false; }
    bool ShouldFlashMainFirmware() const { return should_flash_main_fw_; }

    void OnFlashedOemFirmware() { should_flash_oem_fw_ = false; }
    bool ShouldFlashOemFirmware() const { return should_flash_oem_fw_; }

    void OnFlashedCarrierFirmware(const base::FilePath& path) {
      last_carrier_fw_flashed_ = path;
    }
    bool ShouldFlashCarrierFirmware(const base::FilePath& path) const {
      return last_carrier_fw_flashed_ != path;
    }

    void OnCarrierSeen(const std::string& carrier_id) {
      if (carrier_id == last_carrier_id_)
        return;

      last_carrier_id_ = carrier_id;
      should_flash_main_fw_ = true;
      should_flash_oem_fw_ = true;
    }

    // Used to determine if any FW was installed before reporting metrics.
    bool fw_flashed_ = false;

   private:
    // Unlike carrier firmware, we should usually successfully flash the main
    // firmware at most once per boot. In the past vendors have failed to
    // update the version that the firmware reports itself as so we can mitigate
    // some of the potential issues by recording which modems we have deemed
    // don't need updates or were already updated and avoid checking them again.
    //
    // We should retry flashing the main firmware if the carrier changes since
    // we might have different main firmware versions. As such, when we see a
    // new carrier, reset the |should_flash_main_fw_| for this modem.
    bool should_flash_main_fw_ = true;
    bool should_flash_oem_fw_ = true;
    std::string last_carrier_id_;

    // For carrier firmware, once we've tried to upgrade versions on a
    // particular modem without changing carriers, we should not try to upgrade
    // versions again (but should still flash if the carrier is different) to
    // avoid the same problem as the above. Keep track of the last carrier
    // firmware we flashed so we don't flash twice in a row.
    base::FilePath last_carrier_fw_flashed_;

    // If we fail to flash firmware, we will retry once, but after that we
    // should stop flashing the modem to prevent us from trying it over and
    // over.
    static const int kDefaultTries = 2;
    int tries_ = kDefaultTries;
  };

  // Notify UpdateFirmwareComplete failure and reset the |fw_flashed_| flag.
  void ProcessFailedToPrepareFirmwareFile(const base::Location& code_location,
                                          FlashState* flash_state,
                                          const std::string& firmware_path);

  std::unique_ptr<Journal> journal_;

  std::map<std::string, FlashState> modem_info_;

  // Owned by Daemon
  FirmwareDirectory* firmware_directory_;
  NotificationManager* notification_mgr_;
};

}  // namespace modemfwd

#endif  // MODEMFWD_MODEM_FLASHER_H_
