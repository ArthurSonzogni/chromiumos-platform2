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

#include "modemfwd/error.h"
#include "modemfwd/firmware_file.h"
#include "modemfwd/logging.h"
#include "modemfwd/metrics.h"
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

uint32_t GetFirmwareTypesForMetrics(std::vector<FirmwareConfig> flash_cfg) {
  uint32_t fw_types = 0;
  if (flash_cfg.empty())
    return 0;
  for (const auto& info : flash_cfg) {
    std::string fw_type = info.fw_type;
    if (fw_type == kFwMain)
      fw_types |=
          static_cast<int>(metrics::ModemFirmwareType::kModemFirmwareTypeMain);
    else if (fw_type == kFwOem)
      fw_types |=
          static_cast<int>(metrics::ModemFirmwareType::kModemFirmwareTypeOem);
    else if (fw_type == kFwCarrier)
      fw_types |= static_cast<int>(
          metrics::ModemFirmwareType::kModemFirmwareTypeCarrier);
    else if (fw_type == kFwAp)
      fw_types |=
          static_cast<int>(metrics::ModemFirmwareType::kModemFirmwareTypeAp);
    else if (fw_type == kFwDev)
      fw_types |=
          static_cast<int>(metrics::ModemFirmwareType::kModemFirmwareTypeDev);
    else
      fw_types |= static_cast<int>(
          metrics::ModemFirmwareType::kModemFirmwareTypeUnknown);
  }

  ELOG(INFO) << "metrics_fw_types " << fw_types;

  return fw_types;
}

}  // namespace

ModemFlasher::ModemFlasher(Delegate* delegate,
                           FirmwareDirectory* firmware_directory,
                           Journal* journal,
                           NotificationManager* notification_mgr,
                           Metrics* metrics)
    : delegate_(delegate),
      firmware_directory_(firmware_directory),
      journal_(journal),
      notification_mgr_(notification_mgr),
      metrics_(metrics) {}

bool ModemFlasher::ShouldFlash(Modem* modem, brillo::ErrorPtr* err) {
  std::string equipment_id = modem->GetEquipmentId();
  FlashState* flash_state = &modem_info_[equipment_id];
  if (!flash_state->ShouldFlash()) {
    Error::AddTo(
        err, FROM_HERE, kErrorResultFlashFailure,
        base::StringPrintf("Modem with equipment ID \"%s\" failed to flash too "
                           "many times; not flashing",
                           equipment_id.c_str()));
    return false;
  }

  return true;
}

std::unique_ptr<FlashConfig> ModemFlasher::BuildFlashConfig(
    Modem* modem, brillo::ErrorPtr* err) {
  FlashState* flash_state = &modem_info_[modem->GetEquipmentId()];
  std::string device_id = modem->GetDeviceId();

  std::unique_ptr<FlashConfig> res = std::make_unique<FlashConfig>();
  res->carrier_id = modem->GetCarrierId();

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

  for (const auto& flash_info : flash_infos) {
    const FirmwareFileInfo& file_info = *flash_info.second;
    base::FilePath fw_path = GetFirmwarePath(file_info);
    if (!flash_state->ShouldFlashFirmware(flash_info.first, fw_path))
      continue;

    std::string existing_version = GetFirmwareVersion(modem, flash_info.first);
    ELOG(INFO) << "Found " << flash_info.first << " firmware blob "
               << file_info.version << ", currently installed "
               << flash_info.first << " firmware version: " << existing_version;
    if (file_info.version == existing_version) {
      // We don't need to check the firmware again if there's nothing new.
      // Pretend that we successfully flashed it.
      flash_state->OnFlashedFirmware(flash_info.first, fw_path);
      continue;
    }

    auto firmware_file = std::make_unique<FirmwareFile>();
    if (!firmware_file->PrepareFrom(firmware_directory_->GetFirmwarePath(),
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

bool ModemFlasher::RunFlash(Modem* modem,
                            const FlashConfig& flash_cfg,
                            bool modem_seen_since_oobe,
                            base::TimeDelta* out_duration,
                            brillo::ErrorPtr* err) {
  FlashState* flash_state = &modem_info_[modem->GetEquipmentId()];

  base::Time start = base::Time::Now();
  bool success = modem->FlashFirmwares(flash_cfg.fw_configs);
  if (out_duration)
    *out_duration = base::Time::Now() - start;

  if (!success) {
    flash_state->OnFlashFailed();
    Error::AddTo(err, FROM_HERE,
                 (modem_seen_since_oobe
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

bool ModemFlasher::TryFlash(Modem* modem,
                            bool modem_seen_since_oobe,
                            brillo::ErrorPtr* err) {
  if (!ShouldFlash(modem, err)) {
    notification_mgr_->NotifyUpdateFirmwareCompletedFailure(err->get());
    return false;
  }

  // Clear the attach APN if needed for a specific modem/carrier combination.
  std::string carrier_id = modem->GetCarrierId();
  if (!carrier_id.empty() && !modem->ClearAttachAPN(carrier_id)) {
    ELOG(INFO) << "Clear attach APN failed for current carrier.";
  }

  std::unique_ptr<FlashConfig> flash_cfg = BuildFlashConfig(modem, err);
  if (!flash_cfg) {
    notification_mgr_->NotifyUpdateFirmwareCompletedFailure(err->get());
    return false;
  }

  // End early if we don't have any new firmware.
  if (flash_cfg->fw_configs.empty()) {
    // This message is used by tests to track the end of flashing.
    LOG(INFO) << "The modem already has the correct firmware installed";
    notification_mgr_->NotifyUpdateFirmwareCompletedSuccess(false, 0);
    return true;
  }

  std::string device_id = modem->GetDeviceId();
  InhibitMode _inhibit(modem);

  std::vector<std::string> fw_types;
  std::transform(flash_cfg->fw_configs.begin(), flash_cfg->fw_configs.end(),
                 std::back_inserter(fw_types),
                 [](const FirmwareConfig& cfg) { return cfg.fw_type; });
  std::optional<std::string> entry_id = journal_->MarkStartOfFlashingFirmware(
      fw_types, device_id, flash_cfg->carrier_id);
  if (!entry_id.has_value()) {
    LOG(WARNING) << "Couldn't write operation to journal";
  }

  uint32_t types_for_metrics =
      GetFirmwareTypesForMetrics(flash_cfg->fw_configs);

  base::TimeDelta flash_duration;
  if (!RunFlash(modem, *flash_cfg, modem_seen_since_oobe, &flash_duration,
                err)) {
    if (entry_id.has_value()) {
      journal_->MarkEndOfFlashingFirmware(*entry_id);
    }
    notification_mgr_->NotifyUpdateFirmwareCompletedFlashFailure(
        err->get(), types_for_metrics);
    return false;
  }

  // Report flashing time in successful cases
  metrics_->SendFwFlashTime(flash_duration);
  delegate_->RegisterOnModemReappearanceCallback(
      modem->GetEquipmentId(), base::BindOnce(&ModemFlasher::FlashFinished,
                                              weak_ptr_factory_.GetWeakPtr(),
                                              entry_id, types_for_metrics));
  return true;
}

base::FilePath ModemFlasher::GetFirmwarePath(const FirmwareFileInfo& info) {
  return firmware_directory_->GetFirmwarePath().Append(info.firmware_path);
}

void ModemFlasher::FlashFinished(std::optional<std::string> journal_entry_id,
                                 uint32_t fw_types) {
  if (journal_entry_id.has_value()) {
    journal_->MarkEndOfFlashingFirmware(*journal_entry_id);
  }
  notification_mgr_->NotifyUpdateFirmwareCompletedSuccess(true, fw_types);
}

}  // namespace modemfwd
