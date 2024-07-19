// Copyright 2017 The ChromiumOS Authors
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

#include "base/files/file_path.h"
#include "modemfwd/error.h"
#include "modemfwd/firmware_directory.h"
#include "modemfwd/firmware_file.h"
#include "modemfwd/logging.h"
#include "modemfwd/modem.h"
#include "modemfwd/recovery_file.h"

namespace modemfwd {

namespace {

std::string GetFirmwareVersion(Modem* modem, std::string type) {
  if (type == kFwMain)
    return modem->GetMainFirmwareVersion();
  else if (type == kFwCarrier)
    return modem->GetCarrierFirmwareVersion();
  else if (type == kFwOem)
    return modem->GetOemFirmwareVersion();
  else
    return modem->GetAssocFirmwareVersion(type);
}

class FlashState {
 public:
  FlashState() = default;
  ~FlashState() = default;

  void OnFlashFailed() { tries_--; }
  bool ShouldFlash() const { return tries_ > 0; }

  void OnFlashedFirmware(const std::string& type, const base::FilePath& path) {
    if (type == kFwCarrier) {
      last_carrier_fw_flashed_ = path;
      return;
    }
    flashed_fw_types_.insert(type);
  }

  bool ShouldFlashFirmware(const std::string& type,
                           const base::FilePath& path) {
    if (type == kFwCarrier)
      return last_carrier_fw_flashed_ != path;
    return flashed_fw_types_.count(type) == 0;
  }

  void OnCarrierSeen(const std::string& carrier_id) {
    if (carrier_id == last_carrier_id_)
      return;

    last_carrier_id_ = carrier_id;
    flashed_fw_types_.clear();
  }

 private:
  // Unlike carrier firmware, we should usually successfully flash the main
  // firmware at most once per boot. In the past vendors have failed to
  // update the version that the firmware reports itself as so we can mitigate
  // some of the potential issues by recording which modems we have deemed
  // don't need updates or were already updated and avoid checking them again.
  //
  // We should retry flashing the main firmware if the carrier changes since
  // we might have different main firmware versions. As such, when we see a
  // new carrier, clear the flashed types for this modem.
  std::set<std::string> flashed_fw_types_;
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

}  // namespace

class ModemFlasherImpl : public ModemFlasher {
 public:
  ModemFlasherImpl(FirmwareDirectory* firmware_directory,
                   Prefs* modems_seen_since_oobe_prefs)
      : firmware_directory_(firmware_directory),
        modems_seen_since_oobe_prefs_(modems_seen_since_oobe_prefs) {}
  ModemFlasherImpl(const ModemFlasherImpl&) = delete;
  ModemFlasherImpl& operator=(const ModemFlasherImpl&) = delete;
  ~ModemFlasherImpl() override = default;

  bool ShouldFlash(Modem* modem, brillo::ErrorPtr* err) override {
    std::string equipment_id = modem->GetEquipmentId();
    FlashState* flash_state = &modem_info_[equipment_id];
    if (!flash_state->ShouldFlash()) {
      Error::AddTo(err, FROM_HERE, kErrorResultFlashFailure,
                   base::StringPrintf(
                       "Modem with equipment ID \"%s\" failed to flash too "
                       "many times; not flashing",
                       equipment_id.c_str()));
      return false;
    }

    return true;
  }

  std::unique_ptr<FlashConfig> BuildFlashConfig(
      Modem* modem,
      std::optional<std::string> carrier_override_uuid,
      brillo::ErrorPtr* err) override {
    FlashState* flash_state = &modem_info_[modem->GetEquipmentId()];
    std::string device_id = modem->GetDeviceId();

    std::unique_ptr<FlashConfig> res = std::make_unique<FlashConfig>();
    res->carrier_id = carrier_override_uuid.has_value() ? *carrier_override_uuid
                                                        : modem->GetCarrierId();

    flash_state->OnCarrierSeen(res->carrier_id);
    FirmwareDirectory::Files files = firmware_directory_->FindFirmware(
        device_id, res->carrier_id.empty() ? nullptr : &res->carrier_id);

    std::vector<std::pair<std::string, const FirmwareFileInfo*>> flash_infos;
    if (files.main_firmware.has_value())
      flash_infos.emplace_back(kFwMain, &files.main_firmware.value());
    if (files.oem_firmware.has_value())
      flash_infos.emplace_back(kFwOem, &files.oem_firmware.value());
    for (const auto& assoc_entry : files.assoc_firmware)
      flash_infos.emplace_back(assoc_entry.first, &assoc_entry.second);

    if (!res->temp_extraction_dir_.CreateUniqueTempDir()) {
      LOG(ERROR) << "Failed to create temporary directory for firmware";
      return nullptr;
    }

    if (!PrepareRecoveryFiles(modem->GetHelper(), files, firmware_directory_,
                              res->temp_extraction_dir_.GetPath(),
                              &res->recovery_files)) {
      Error::AddTo(err, FROM_HERE, kErrorResultFailedToPrepareFirmwareFile,
                   base::StringPrintf("Failed to prepare recovery files"));
      return nullptr;
    }

    for (const auto& flash_info : flash_infos) {
      const FirmwareFileInfo& file_info = *flash_info.second;
      base::FilePath fw_path = GetFirmwarePath(file_info);
      if (!flash_state->ShouldFlashFirmware(flash_info.first, fw_path))
        continue;

      std::string existing_version =
          GetFirmwareVersion(modem, flash_info.first);
      ELOG(INFO) << "Found " << flash_info.first << " firmware blob "
                 << file_info.version << ", currently installed "
                 << flash_info.first
                 << " firmware version: " << existing_version;
      if (file_info.version == existing_version) {
        // We don't need to check the firmware again if there's nothing new.
        // Pretend that we successfully flashed it.
        flash_state->OnFlashedFirmware(flash_info.first, fw_path);
        continue;
      }

      auto firmware_file = std::make_unique<FirmwareFile>();
      if (!firmware_file->PrepareFrom(firmware_directory_->GetFirmwarePath(),
                                      res->temp_extraction_dir_.GetPath(),
                                      file_info)) {
        Error::AddTo(err, FROM_HERE, kErrorResultFailedToPrepareFirmwareFile,
                     base::StringPrintf("Failed to prepare firmware file: %s",
                                        fw_path.value().c_str()));
        return nullptr;
      }

      // We found different firmware! Add it to the list of firmware to flash.
      res->fw_configs.push_back({flash_info.first,
                                 firmware_file->path_on_filesystem(),
                                 file_info.version});
      res->files[flash_info.first] = std::move(firmware_file);
    }

    // Check if we need to update the carrier firmware.
    if (res->carrier_id.empty()) {
      ELOG(INFO) << "No carrier found. Is a SIM card inserted?";
      return res;
    }
    if (!files.carrier_firmware.has_value()) {
      ELOG(INFO) << "No carrier firmware found for carrier " << res->carrier_id;
      return res;
    }

    const FirmwareFileInfo& file_info = files.carrier_firmware.value();
    base::FilePath fw_path = GetFirmwarePath(file_info);
    if (!flash_state->ShouldFlashFirmware(kFwCarrier, fw_path)) {
      ELOG(INFO) << "Already flashed carrier firmware for " << res->carrier_id;
      return res;
    }

    ELOG(INFO) << "Found carrier firmware blob " << file_info.version
               << " for carrier " << res->carrier_id;

    // Carrier firmware operates a bit differently. We need to flash if
    // the carrier or the version has changed, or if there wasn't any carrier
    // firmware to begin with.
    std::string carrier_fw_id = modem->GetCarrierFirmwareId();
    std::string carrier_fw_version = modem->GetCarrierFirmwareVersion();
    if (carrier_fw_id.empty() || carrier_fw_version.empty()) {
      ELOG(INFO) << "No carrier firmware is currently installed";
    } else {
      ELOG(INFO) << "Currently installed carrier firmware version "
                 << carrier_fw_version << " for carrier " << carrier_fw_id;
      if (firmware_directory_->IsUsingSameFirmware(device_id, carrier_fw_id,
                                                   res->carrier_id) &&
          carrier_fw_version == file_info.version) {
        ELOG(INFO) << "Correct carrier firmware is already installed";
        return res;
      }
    }

    auto firmware_file = std::make_unique<FirmwareFile>();
    if (!firmware_file->PrepareFrom(firmware_directory_->GetFirmwarePath(),
                                    res->temp_extraction_dir_.GetPath(),
                                    file_info)) {
      Error::AddTo(err, FROM_HERE, kErrorResultFailedToPrepareFirmwareFile,
                   base::StringPrintf("Failed to prepare firmware file: %s",
                                      fw_path.value().c_str()));
      return nullptr;
    }

    res->fw_configs.push_back(
        {kFwCarrier, firmware_file->path_on_filesystem(), file_info.version});
    res->files[kFwCarrier] = std::move(firmware_file);

    return res;
  }

  bool RunFlash(Modem* modem,
                const FlashConfig& flash_cfg,
                base::TimeDelta* out_duration,
                brillo::ErrorPtr* err) override {
    FlashState* flash_state = &modem_info_[modem->GetEquipmentId()];

    base::Time start = base::Time::Now();
    bool success = modem->FlashFirmwares(flash_cfg.fw_configs);
    if (out_duration)
      *out_duration = base::Time::Now() - start;

    if (!success) {
      flash_state->OnFlashFailed();
      Error::AddTo(err, FROM_HERE,
                   (modems_seen_since_oobe_prefs_->Exists(modem->GetDeviceId())
                        ? kErrorResultFailureReturnedByHelper
                        : kErrorResultFailureReturnedByHelperModemNeverSeen),
                   "Helper failed to flash firmware files");
      return false;
    }

    for (const auto& info : flash_cfg.fw_configs) {
      std::string fw_type = info.fw_type;
      base::FilePath path_for_logging =
          flash_cfg.files.at(fw_type)->path_for_logging();
      flash_state->OnFlashedFirmware(fw_type, path_for_logging);
      ELOG(INFO) << "Flashed " << fw_type << " firmware (" << path_for_logging
                 << ") to the modem";
    }

    return true;
  }

 private:
  base::FilePath GetFirmwarePath(const FirmwareFileInfo& info) {
    return firmware_directory_->GetFirmwarePath().Append(info.firmware_path);
  }

  std::map<std::string, FlashState> modem_info_;

  // Owned by Daemon
  FirmwareDirectory* firmware_directory_;
  Prefs* modems_seen_since_oobe_prefs_;

  base::WeakPtrFactory<ModemFlasher> weak_ptr_factory_{this};
};

std::unique_ptr<ModemFlasher> CreateModemFlasher(
    FirmwareDirectory* firmware_directory,
    Prefs* modems_seen_since_oobe_prefs) {
  return std::make_unique<ModemFlasherImpl>(firmware_directory,
                                            modems_seen_since_oobe_prefs);
}

}  // namespace modemfwd
