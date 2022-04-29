// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/modem_flasher.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/switches/modemfwd_switches.h>
#include <dbus/modemfwd/dbus-constants.h>

#include "modemfwd/error.h"
#include "modemfwd/firmware_file.h"
#include "modemfwd/logging.h"
#include "modemfwd/modem.h"
#include "modemfwd/notification_manager.h"

namespace modemfwd {

namespace {

class InhibitMode {
 public:
  explicit InhibitMode(Modem* modem) : modem_(modem) {
    if (!modem_->SetInhibited(true))
      ELOG(INFO) << "Inhibiting failed";
  }
  ~InhibitMode() {
    if (!modem_->SetInhibited(false))
      ELOG(INFO) << "Uninhibiting failed";
  }

 private:
  Modem* modem_;
};

}  // namespace

ModemFlasher::ModemFlasher(FirmwareDirectory* firmware_directory,
                           std::unique_ptr<Journal> journal,
                           NotificationManager* notification_mgr)
    : journal_(std::move(journal)),
      firmware_directory_(firmware_directory),
      notification_mgr_(notification_mgr) {}

void ModemFlasher::ProcessFailedToPrepareFirmwareFile(
    const base::Location& code_location,
    FlashState* flash_state,
    const std::string& firmware_path) {
  auto err =
      Error::Create(code_location, kErrorResultFailedToPrepareFirmwareFile,
                    base::StringPrintf("Failed to prepare firmware file: %s",
                                       firmware_path.c_str()));
  notification_mgr_->NotifyUpdateFirmwareCompletedFailure(err.get());
  flash_state->fw_flashed_ = false;
}

base::OnceClosure ModemFlasher::TryFlashForTesting(Modem* modem,
                                                   const std::string& variant) {
  firmware_directory_->OverrideVariantForTesting(variant);
  return TryFlash(modem);
}

base::OnceClosure ModemFlasher::TryFlash(Modem* modem) {
  std::string equipment_id = modem->GetEquipmentId();
  FlashState* flash_state = &modem_info_[equipment_id];
  if (!flash_state->ShouldFlash()) {
    auto err = Error::Create(
        FROM_HERE, kErrorResultFlashFailure,
        base::StringPrintf("Modem with equipment ID \"%s\" failed to flash too "
                           "many times; not flashing",
                           equipment_id.c_str()));
    notification_mgr_->NotifyUpdateFirmwareCompletedFailure(err.get());
    flash_state->fw_flashed_ = false;
    return base::OnceClosure();
  }

  std::string device_id = modem->GetDeviceId();
  std::string current_carrier = modem->GetCarrierId();
  // The real carrier ID before it might be replaced by the generic one
  std::string real_carrier = current_carrier;
  flash_state->OnCarrierSeen(current_carrier);
  FirmwareDirectory::Files files = firmware_directory_->FindFirmware(
      device_id, current_carrier.empty() ? nullptr : &current_carrier);

  // Clear the attach APN if needed for a specific modem/carrier combination.
  if (!real_carrier.empty() && !modem->ClearAttachAPN(real_carrier))
    ELOG(INFO) << "Clear attach APN failed for current carrier.";

  std::vector<FirmwareConfig> flash_cfg;
  std::map<std::string, std::unique_ptr<FirmwareFile>> flash_files;
  // Check if we need to update the main firmware.
  if (flash_state->ShouldFlashMainFirmware() &&
      files.main_firmware.has_value()) {
    const FirmwareFileInfo& file_info = files.main_firmware.value();
    ELOG(INFO) << "Found main firmware blob " << file_info.version
               << ", currently installed main firmware version: "
               << modem->GetMainFirmwareVersion();
    if (file_info.version == modem->GetMainFirmwareVersion()) {
      // We don't need to check the main firmware again if there's nothing new.
      // Pretend that we successfully flashed it.
      flash_state->OnFlashedMainFirmware();
    } else {
      auto firmware_file = std::make_unique<FirmwareFile>();
      if (!firmware_file->PrepareFrom(firmware_directory_->GetFirmwarePath(),
                                      file_info)) {
        ProcessFailedToPrepareFirmwareFile(FROM_HERE, flash_state,
                                           file_info.firmware_path);
        return base::OnceClosure();
      }

      // We found different firmware!
      // record to flash the main firmware binary.
      flash_cfg.push_back(
          {kFwMain, firmware_file->path_on_filesystem(), file_info.version});
      flash_files[kFwMain] = std::move(firmware_file);

      // If there are associated firmwares, we also need to prepare those.
      for (const auto& assoc_entry : files.assoc_firmware) {
        auto assoc_file = std::make_unique<FirmwareFile>();
        if (!assoc_file->PrepareFrom(firmware_directory_->GetFirmwarePath(),
                                     assoc_entry.second)) {
          ProcessFailedToPrepareFirmwareFile(
              FROM_HERE, flash_state, assoc_entry.second.firmware_path, err);
          return base::OnceClosure();
        }
        flash_cfg.push_back({assoc_entry.first,
                             assoc_file->path_on_filesystem(),
                             assoc_entry.second.version});
        flash_files[assoc_entry.first] = std::move(assoc_file);
      }
    }
  }

  // Check if we need to update the OEM firmware.
  if (flash_state->ShouldFlashOemFirmware() && files.oem_firmware.has_value()) {
    const FirmwareFileInfo& file_info = files.oem_firmware.value();
    ELOG(INFO) << "Found OEM firmware blob " << file_info.version
               << ", currently installed OEM firmware version: "
               << modem->GetOemFirmwareVersion();
    if (file_info.version == modem->GetOemFirmwareVersion()) {
      flash_state->OnFlashedOemFirmware();
    } else {
      auto firmware_file = std::make_unique<FirmwareFile>();
      if (!firmware_file->PrepareFrom(firmware_directory_->GetFirmwarePath(),
                                      file_info)) {
        ProcessFailedToPrepareFirmwareFile(FROM_HERE, flash_state,
                                           file_info.firmware_path);
        return base::OnceClosure();
      }

      flash_cfg.push_back(
          {kFwOem, firmware_file->path_on_filesystem(), file_info.version});
      flash_files[kFwOem] = std::move(firmware_file);
    }
  }

  // Check if we need to update the carrier firmware.
  if (!current_carrier.empty() && files.carrier_firmware.has_value() &&
      flash_state->ShouldFlashCarrierFirmware(
          firmware_directory_->GetFirmwarePath().Append(
              files.carrier_firmware.value().firmware_path))) {
    const FirmwareFileInfo& file_info = files.carrier_firmware.value();

    ELOG(INFO) << "Found carrier firmware blob " << file_info.version
               << " for carrier " << current_carrier;

    // Carrier firmware operates a bit differently. We need to flash if
    // the carrier or the version has changed, or if there wasn't any carrier
    // firmware to begin with.
    std::string carrier_fw_id = modem->GetCarrierFirmwareId();
    std::string carrier_fw_version = modem->GetCarrierFirmwareVersion();
    bool has_carrier_fw =
        !(carrier_fw_id.empty() || carrier_fw_version.empty());
    if (has_carrier_fw) {
      ELOG(INFO) << "Currently installed carrier firmware version "
                 << carrier_fw_version << " for carrier " << carrier_fw_id;
    } else {
      ELOG(INFO) << "No carrier firmware is currently installed";
    }

    if (!has_carrier_fw ||
        !firmware_directory_->IsUsingSameFirmware(device_id, carrier_fw_id,
                                                  current_carrier) ||
        carrier_fw_version != file_info.version) {
      auto firmware_file = std::make_unique<FirmwareFile>();
      if (!firmware_file->PrepareFrom(firmware_directory_->GetFirmwarePath(),
                                      file_info)) {
        ProcessFailedToPrepareFirmwareFile(FROM_HERE, flash_state,
                                           file_info.firmware_path);
        return base::OnceClosure();
      }

      flash_cfg.push_back(
          {kFwCarrier, firmware_file->path_on_filesystem(), file_info.version});
      flash_files[kFwCarrier] = std::move(firmware_file);
    }
  } else {
    // Log why we are not flashing the carrier firmware for debug
    if (current_carrier.empty()) {
      ELOG(INFO) << "No carrier found. Is a SIM card inserted?";
    } else if (!files.carrier_firmware.has_value()) {
      // Check if we have carrier firmware matching the SIM's carrier. If not,
      // there's nothing to flash.
      ELOG(INFO) << "No carrier firmware found for carrier " << current_carrier;
    } else {
      // ShouldFlashCarrierFirmware() was false
      ELOG(INFO) << "Already flashed carrier firmware for " << current_carrier;
    }
  }

  // Flash if we have new firmwares
  if (flash_cfg.empty()) {
    // This message is used by tests to track the end of flashing.
    LOG(INFO) << "The modem already has the correct firmware installed";
    notification_mgr_->NotifyUpdateFirmwareCompletedSuccess(
        flash_state->fw_flashed_);
    flash_state->fw_flashed_ = false;
    return base::OnceClosure();
  }
  std::vector<std::string> fw_types;
  std::transform(flash_cfg.begin(), flash_cfg.end(),
                 std::back_inserter(fw_types),
                 [](const FirmwareConfig& cfg) { return cfg.fw_type; });

  InhibitMode _inhibit(modem);
  journal_->MarkStartOfFlashingFirmware(fw_types, device_id, current_carrier);
  if (!modem->FlashFirmwares(flash_cfg)) {
    flash_state->OnFlashFailed();
    journal_->MarkEndOfFlashingFirmware(device_id, current_carrier);
    auto err = Error::Create(FROM_HERE, kErrorResultFailureReturnedByHelper,
                             "Helper failed to flash firmware files");
    notification_mgr_->NotifyUpdateFirmwareCompletedFailure(err.get());
    flash_state->fw_flashed_ = false;
    return base::OnceClosure();
  }
  flash_state->fw_flashed_ = true;

  for (const auto& info : flash_cfg) {
    std::string fw_type = info.fw_type;
    base::FilePath path_for_logging = flash_files[fw_type]->path_for_logging();
    if (fw_type == kFwMain)
      flash_state->OnFlashedMainFirmware();
    else if (fw_type == kFwOem)
      flash_state->OnFlashedOemFirmware();
    else if (fw_type == kFwCarrier)
      flash_state->OnFlashedCarrierFirmware(path_for_logging);
    ELOG(INFO) << "Flashed " << fw_type << " firmware (" << path_for_logging
               << ") to the modem";
  }
  return base::BindOnce(&Journal::MarkEndOfFlashingFirmware,
                        base::Unretained(journal_.get()), device_id,
                        current_carrier);
}

}  // namespace modemfwd
