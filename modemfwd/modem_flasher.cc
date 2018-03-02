// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/modem_flasher.h"

#include <memory>
#include <utility>

#include <base/stl_util.h>

#include "modemfwd/modem.h"

namespace modemfwd {

ModemFlasher::ModemFlasher(
    std::unique_ptr<FirmwareDirectory> firmware_directory,
    std::unique_ptr<Journal> journal)
    : firmware_directory_(std::move(firmware_directory)),
      journal_(std::move(journal)) {}

void ModemFlasher::TryFlash(Modem* modem) {
  std::string equipment_id = modem->GetEquipmentId();
  if (base::ContainsKey(blacklist_, equipment_id)) {
    LOG(WARNING) << "Modem with equipment ID \"" << equipment_id
                 << "\" is blacklisted; not flashing";
    return;
  }
  std::string device_id = modem->GetDeviceId();

  FirmwareFileInfo file_info;
  // Check if we need to update the main firmware.
  if (!base::ContainsKey(main_fw_checked_, equipment_id) &&
      firmware_directory_->FindMainFirmware(device_id, &file_info)) {
    DLOG(INFO) << "Found main firmware blob " << file_info.version
               << ", currently installed main firmware version: "
               << modem->GetMainFirmwareVersion();
    if (file_info.version != modem->GetMainFirmwareVersion()) {
      // We found different firmware! Flash the modem, and since it will
      // reboot afterwards, we can wait to get called again to check the
      // carrier firmware.
      journal_->MarkStartOfFlashingMainFirmware(device_id);
      if (modem->FlashMainFirmware(file_info.firmware_path)) {
        main_fw_checked_.insert(equipment_id);
        DLOG(INFO) << "Flashed " << file_info.firmware_path.value()
                   << " to the modem";
      } else {
        blacklist_.insert(equipment_id);
      }
      journal_->MarkEndOfFlashingMainFirmware(device_id);
      return;
    }
  }

  // We don't need to check the main firmware again if there's nothing new.
  main_fw_checked_.insert(equipment_id);

  // If there's no SIM, we can stop here.
  std::string current_carrier = modem->GetCarrierId();
  if (current_carrier.empty()) {
    DLOG(INFO) << "No carrier found. Is a SIM card inserted?";
    return;
  }

  // Check if we have carrier firmware matching the SIM's carrier. If not,
  // there's nothing to flash.
  if (!firmware_directory_->FindCarrierFirmware(device_id, &current_carrier,
                                                &file_info)) {
    DLOG(INFO) << "No carrier firmware found for carrier " << current_carrier;
    return;
  }

  auto it = last_carrier_fw_flashed_.find(equipment_id);
  if (it != last_carrier_fw_flashed_.end() &&
      it->second == file_info.firmware_path) {
    DLOG(INFO) << "Already flashed carrier firmware for " << current_carrier;
    return;
  }

  DLOG(INFO) << "Found carrier firmware blob " << file_info.version
             << " for carrier " << current_carrier;

  // Carrier firmware operates a bit differently. We need to flash if
  // the carrier or the version has changed, or if there wasn't any carrier
  // firmware to begin with.
  std::string carrier_fw_id = modem->GetCarrierFirmwareId();
  std::string carrier_fw_version = modem->GetCarrierFirmwareVersion();
  bool has_carrier_fw = !(carrier_fw_id.empty() || carrier_fw_version.empty());
  if (has_carrier_fw) {
    DLOG(INFO) << "Currently installed carrier firmware version "
               << carrier_fw_version << " for carrier " << carrier_fw_id;
  } else {
    DLOG(INFO) << "No carrier firmware is currently installed";
  }

  if (!has_carrier_fw || carrier_fw_id != current_carrier ||
      carrier_fw_version != file_info.version) {
    journal_->MarkStartOfFlashingCarrierFirmware(device_id, current_carrier);
    if (modem->FlashCarrierFirmware(file_info.firmware_path)) {
      last_carrier_fw_flashed_.insert(
          std::make_pair(equipment_id, file_info.firmware_path));
      DLOG(INFO) << "Flashed " << file_info.firmware_path.value()
                 << " to the modem";
    } else {
      blacklist_.insert(equipment_id);
    }
    journal_->MarkEndOfFlashingCarrierFirmware(device_id, current_carrier);
  }
}

}  // namespace modemfwd
