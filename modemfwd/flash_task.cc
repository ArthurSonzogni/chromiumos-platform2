// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/flash_task.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <dbus/modemfwd/dbus-constants.h>

#include "modemfwd/error.h"
#include "modemfwd/logging.h"
#include "modemfwd/modem.h"
#include "modemfwd/modem_helper.h"

namespace modemfwd {

namespace {

uint32_t GetFirmwareTypesForMetrics(std::vector<FirmwareConfig> flash_cfg) {
  uint32_t fw_types = 0;
  if (flash_cfg.empty())
    return 0;
  for (const auto& info : flash_cfg) {
    std::string fw_type = info.fw_type;
    if (fw_type == kFwMain) {
      fw_types |=
          static_cast<int>(metrics::ModemFirmwareType::kModemFirmwareTypeMain);
    } else if (fw_type == kFwOem) {
      fw_types |=
          static_cast<int>(metrics::ModemFirmwareType::kModemFirmwareTypeOem);
    } else if (fw_type == kFwCarrier) {
      fw_types |= static_cast<int>(
          metrics::ModemFirmwareType::kModemFirmwareTypeCarrier);
    } else if (fw_type == kFwAp) {
      fw_types |=
          static_cast<int>(metrics::ModemFirmwareType::kModemFirmwareTypeAp);
    } else if (fw_type == kFwDev) {
      fw_types |=
          static_cast<int>(metrics::ModemFirmwareType::kModemFirmwareTypeDev);
    } else {
      fw_types |= static_cast<int>(
          metrics::ModemFirmwareType::kModemFirmwareTypeUnknown);
    }
  }

  ELOG(INFO) << "metrics_fw_types " << fw_types;

  return fw_types;
}

}  // namespace

// static
int FlashTask::num_flash_tasks_ = 0;

FlashTask::FlashTask(Delegate* delegate,
                     Journal* journal,
                     NotificationManager* notification_mgr,
                     Metrics* metrics,
                     scoped_refptr<dbus::Bus> bus,
                     scoped_refptr<AsyncModemFlasher> modem_flasher)
    : Task(delegate,
           "flash-" + std::to_string(++num_flash_tasks_),
           kTaskTypeFlash),
      journal_(journal),
      notification_mgr_(notification_mgr),
      metrics_(metrics),
      bus_(bus),
      modem_flasher_(modem_flasher) {}

void FlashTask::Start(Modem* modem, const FlashTask::Options& options) {
  SetProp(kFlashTaskForceFlash, options.should_always_flash);
  if (options.carrier_override_uuid.has_value()) {
    SetProp(kFlashTaskCarrierOverride, *options.carrier_override_uuid);
  }

  if (options.should_always_flash) {
    OnShouldFlashCompleted(modem, options, true, nullptr);
  } else {
    modem_flasher_->ShouldFlash(
        modem, base::BindOnce(&FlashTask::OnShouldFlashCompleted,
                              weak_ptr_factory_.GetWeakPtr(), modem, options));
  }
}

void FlashTask::OnShouldFlashCompleted(Modem* modem,
                                       const Options& options,
                                       bool should_flash,
                                       brillo::ErrorPtr err) {
  if (!should_flash) {
    notification_mgr_->NotifyUpdateFirmwareCompletedFailure(err.get());
    Finish(std::move(err));
    return;
  }

  // Clear the attach APN if needed for a specific modem/carrier combination.
  std::string carrier_id = modem->GetCarrierId();
  if (!carrier_id.empty() && !modem->ClearAttachAPN(carrier_id)) {
    ELOG(INFO) << "Clear attach APN failed for current carrier.";
  }

  modem_flasher_->BuildFlashConfig(
      modem, options.carrier_override_uuid,
      base::BindOnce(&FlashTask::OnBuildFlashConfigCompleted,
                     weak_ptr_factory_.GetWeakPtr(), modem));
}

void FlashTask::OnBuildFlashConfigCompleted(
    Modem* modem,
    std::unique_ptr<FlashConfig> flash_cfg,
    brillo::ErrorPtr err) {
  if (!flash_cfg) {
    notification_mgr_->NotifyUpdateFirmwareCompletedFailure(err.get());
    Finish(std::move(err));
    return;
  }

  // End early if we don't have any new firmware.
  if (flash_cfg->fw_configs.empty()) {
    // This message is used by tests to track the end of flashing.
    LOG(INFO) << "The modem already has the correct firmware installed";
    notification_mgr_->NotifyUpdateFirmwareCompletedSuccess(false, 0);
    Finish();
    return;
  }

  std::string device_id = modem->GetDeviceId();
  auto inhibiter = std::make_unique<InhibitMode>(modem);

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

  std::vector<std::unique_ptr<UpstartJobController>> jobs;
  // The upstart job controller will restart this job when the object is
  // destroyed.
  auto hermes = std::make_unique<UpstartJobController>(
      UpstartJobController::kHermesJobPath, bus_);
  if (hermes->IsRunning()) {
    hermes->Stop();
  }
  jobs.push_back(std::move(hermes));

  std::vector<std::string> upstart_in_env;
  for (const auto& config : flash_cfg->fw_configs) {
    upstart_in_env.push_back(base::StringPrintf("%s=%s", config.fw_type.c_str(),
                                                config.path.value().c_str()));
  }

  // If installed, modemfwd-helpers.conf may be used to perform actions with
  // the fw that only root can perform. upstart_in_env must be checked by
  // modemfwd-helpers.conf.
  auto modemfwd_helpers = std::make_unique<UpstartJobController>(
      UpstartJobController::kModemHelperJobPath, bus_);
  if (modemfwd_helpers->IsInstalled() &&
      !modemfwd_helpers->Start(upstart_in_env)) {
    Finish(Error::Create(FROM_HERE, kErrorResultFlashFailure,
                         "Failed to start modemfwd-helpers"));
    return;
  }
  jobs.push_back(std::move(modemfwd_helpers));

  modem_flasher_->RunFlash(modem, std::move(flash_cfg),
                           base::BindOnce(&FlashTask::OnRunFlashCompleted,
                                          weak_ptr_factory_.GetWeakPtr(), modem,
                                          std::move(inhibiter), std::move(jobs),
                                          entry_id, types_for_metrics));
}

void FlashTask::OnRunFlashCompleted(
    Modem* modem,
    std::unique_ptr<InhibitMode> inhibiter,
    std::vector<std::unique_ptr<UpstartJobController>> upstart_jobs,
    std::optional<std::string> journal_entry_id,
    int32_t types_for_metrics,
    bool success,
    base::TimeDelta flash_duration,
    brillo::ErrorPtr err) {
  inhibiter.reset();
  upstart_jobs.clear();

  if (!success) {
    if (journal_entry_id.has_value()) {
      journal_->MarkEndOfFlashingFirmware(*journal_entry_id);
    }
    notification_mgr_->NotifyUpdateFirmwareCompletedFlashFailure(
        err.get(), types_for_metrics);
    Finish(std::move(err));
    return;
  }

  // Report flashing time in successful cases
  metrics_->SendFwFlashTime(flash_duration);
  if (modem->IsPresent()) {
    delegate()->RegisterOnModemReappearanceCallback(
        modem->GetEquipmentId(),
        base::BindOnce(&FlashTask::FlashFinished,
                       weak_ptr_factory_.GetWeakPtr(), journal_entry_id,
                       types_for_metrics));
  } else {
    // Just run this inline as we can't be sure the modem will ever come back
    // on the bus and don't want to leave journal entries lying around.
    FlashFinished(journal_entry_id, types_for_metrics);
  }
}

void FlashTask::FlashFinished(std::optional<std::string> journal_entry_id,
                              uint32_t fw_types) {
  if (journal_entry_id.has_value()) {
    journal_->MarkEndOfFlashingFirmware(*journal_entry_id);
  }
  notification_mgr_->NotifyUpdateFirmwareCompletedSuccess(true, fw_types);
  Finish();
}

}  // namespace modemfwd
