// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/modem_flasher.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <chromeos/switches/modemfwd_switches.h>

#include "modemfwd/firmware_file.h"
#include "modemfwd/logging.h"
#include "modemfwd/modem.h"

namespace {

constexpr char kDisableAutoUpdatePref[] =
    "/var/lib/modemfwd/disable_auto_update";

}  // namespace

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

bool IsAutoUpdateDisabledByPref() {
  const base::FilePath pref_path(kDisableAutoUpdatePref);
  std::string contents;
  if (!base::ReadFileToString(pref_path, &contents))
    return false;

  contents = base::TrimWhitespaceASCII(contents, base::TRIM_ALL).as_string();
  int pref_value;
  if (!base::StringToInt(contents, &pref_value))
    return false;

  return (pref_value == 1);
}

ModemFlasher::ModemFlasher(
    std::unique_ptr<FirmwareDirectory> firmware_directory,
    std::unique_ptr<Journal> journal)
    : firmware_directory_(std::move(firmware_directory)),
      journal_(std::move(journal)) {}

base::Closure ModemFlasher::TryFlash(Modem* modem) {
  if (IsAutoUpdateDisabledByPref()) {
    LOG(INFO) << "Update disabled by pref";
    return base::Closure();
  }

  std::string equipment_id = modem->GetEquipmentId();
  FlashState* flash_state = &modem_info_[equipment_id];
  if (!flash_state->ShouldFlash()) {
    LOG(WARNING) << "Modem with equipment ID \"" << equipment_id
                 << "\" failed to flash too many times; not flashing";
    return base::Closure();
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
      if (!firmware_file->PrepareFrom(file_info))
        return base::Closure();

      // We found different firmware!
      // record to flash the main firmware binary.
      flash_cfg.push_back(
          {kFwMain, firmware_file->path_on_filesystem(), file_info.version});
      flash_files[kFwMain] = std::move(firmware_file);
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
      if (!firmware_file->PrepareFrom(file_info))
        return base::Closure();

      flash_cfg.push_back(
          {kFwOem, firmware_file->path_on_filesystem(), file_info.version});
      flash_files[kFwOem] = std::move(firmware_file);
    }
  }

  // Check if we need to update the carrier firmware.
  if (!current_carrier.empty() && files.carrier_firmware.has_value() &&
      flash_state->ShouldFlashCarrierFirmware(
          files.carrier_firmware.value().firmware_path)) {
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
      if (!firmware_file->PrepareFrom(file_info))
        return base::Closure();

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
  if (flash_cfg.empty())
    return base::Closure();

  std::vector<std::string> fw_types;
  std::transform(flash_cfg.begin(), flash_cfg.end(),
                 std::back_inserter(fw_types),
                 [](const FirmwareConfig& cfg) { return cfg.fw_type; });

  InhibitMode _inhibit(modem);
  journal_->MarkStartOfFlashingFirmware(fw_types, device_id, current_carrier);
  if (!modem->FlashFirmwares(flash_cfg)) {
    flash_state->OnFlashFailed();
    journal_->MarkEndOfFlashingFirmware(device_id, current_carrier);
    return base::Closure();
  }

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
  return base::Bind(&Journal::MarkEndOfFlashingFirmware,
                    base::Unretained(journal_.get()), device_id,
                    current_carrier);
}

}  // namespace modemfwd
