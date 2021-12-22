// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/daemon.h"

#include <sysexits.h>

#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/threading/thread_task_runner_handle.h>
#include <base/time/time.h>
#include <chromeos/dbus/service_constants.h>
#include <cros_config/cros_config.h>

#include "modemfwd/firmware_directory.h"
#include "modemfwd/logging.h"
#include "modemfwd/modem.h"
#include "modemfwd/modem_flasher.h"
#include "modemfwd/modem_helper_directory.h"
#include "modemfwd/modem_tracker.h"

namespace {

constexpr base::TimeDelta kWedgeCheckDelay = base::TimeDelta::FromMinutes(5);
constexpr base::TimeDelta kRebootCheckDelay = base::TimeDelta::FromMinutes(1);
constexpr char kDisableAutoUpdatePref[] =
    "/var/lib/modemfwd/disable_auto_update";

std::string ToOnOffString(bool b) {
  return b ? "on" : "off";
}

// Returns the delay to wait before rebooting the modem if it hasn't appeared
// on the USB bus by reading the /modem/wedge-reboot-delay-ms property of the
// current model via chromeos-config, or using the default `kWedgeCheckDelay`
// constant if it fails to read it from chromeos-config or nothing is specified.
base::TimeDelta GetModemWedgeCheckDelay() {
  brillo::CrosConfig config;
  if (!config.Init()) {
    LOG(WARNING) << "Failed to load Chrome OS configuration";
    return kWedgeCheckDelay;
  }

  std::string delay_ms;
  if (!config.GetString("/modem", "wedge-reboot-delay-ms", &delay_ms)) {
    return kWedgeCheckDelay;
  }

  int64_t ms;
  if (!base::StringToInt64(delay_ms, &ms)) {
    LOG(WARNING) << "Invalid wedge-reboot-delay-ms attribute " << delay_ms
                 << " using default " << kWedgeCheckDelay;
    return kWedgeCheckDelay;
  }

  base::TimeDelta wedge_delay = base::TimeDelta::FromMilliseconds(ms);
  LOG(INFO) << "Use customized wedge reboot delay: " << wedge_delay;
  return wedge_delay;
}

bool IsAutoUpdateDisabledByPref() {
  const base::FilePath pref_path(kDisableAutoUpdatePref);
  std::string contents;
  if (!base::ReadFileToString(pref_path, &contents))
    return false;

  int pref_value;
  if (!base::StringToInt(base::TrimWhitespaceASCII(contents, base::TRIM_ALL),
                         &pref_value))
    return false;

  return (pref_value == 1);
}

}  // namespace

namespace modemfwd {

DBusAdaptor::DBusAdaptor(scoped_refptr<dbus::Bus> bus, Daemon* daemon)
    : org::chromium::ModemfwdAdaptor(this),
      dbus_object_(nullptr, bus, dbus::ObjectPath(kModemfwdServicePath)),
      daemon_(daemon) {
  DCHECK(daemon);
}

void DBusAdaptor::RegisterAsync(
    const brillo::dbus_utils::AsyncEventSequencer::CompletionAction& cb) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(cb);
}

void DBusAdaptor::SetDebugMode(bool debug_mode) {
  g_extra_logging = debug_mode;
  LOG(INFO) << "Debug mode is now " << ToOnOffString(ELOG_IS_ON());
}

bool DBusAdaptor::ForceFlash(const std::string& device_id,
                             const brillo::VariantDictionary& args) {
  std::string carrier_uuid =
      brillo::GetVariantValueOrDefault<std::string>(args, "carrier_uuid");
  std::string variant =
      brillo::GetVariantValueOrDefault<std::string>(args, "variant");
  return daemon_->ForceFlashForTesting(device_id, carrier_uuid, variant);
}

Daemon::Daemon(const std::string& journal_file,
               const std::string& helper_directory)
    : Daemon(journal_file, helper_directory, "") {}

Daemon::Daemon(const std::string& journal_file,
               const std::string& helper_directory,
               const std::string& firmware_directory)
    : DBusServiceDaemon(kModemfwdServiceName),
      journal_file_path_(journal_file),
      helper_dir_path_(helper_directory),
      firmware_dir_path_(firmware_directory),
      weak_ptr_factory_(this) {}

int Daemon::OnInit() {
  int exit_code = brillo::DBusServiceDaemon::OnInit();
  if (exit_code != EX_OK)
    return exit_code;
  DCHECK(!helper_dir_path_.empty());

  if (!base::DirectoryExists(helper_dir_path_)) {
    LOG(ERROR) << "Supplied modem-specific helper directory "
               << helper_dir_path_.value() << " does not exist";
    return EX_UNAVAILABLE;
  }

  helper_directory_ = CreateModemHelperDirectory(helper_dir_path_);
  if (!helper_directory_) {
    LOG(ERROR) << "No suitable helpers found in " << helper_dir_path_.value();
    return EX_UNAVAILABLE;
  }

  // If no firmware directory was supplied, we can't run yet. This will
  // change when we get DLC functionality.
  if (firmware_dir_path_.empty())
    return EX_UNAVAILABLE;

  if (!base::DirectoryExists(firmware_dir_path_)) {
    LOG(ERROR) << "Supplied firmware directory " << firmware_dir_path_.value()
               << " does not exist";
    return EX_UNAVAILABLE;
  }

  return CompleteInitialization();
}

int Daemon::CompleteInitialization() {
  CHECK(!firmware_dir_path_.empty());

  auto firmware_directory = CreateFirmwareDirectory(firmware_dir_path_);
  if (!firmware_directory) {
    LOG(ERROR) << "Could not load firmware directory (bad manifest?)";
    return EX_UNAVAILABLE;
  }

  auto journal = OpenJournal(journal_file_path_, firmware_directory.get(),
                             helper_directory_.get());
  if (!journal) {
    LOG(ERROR) << "Could not open journal file";
    return EX_UNAVAILABLE;
  }

  modem_flasher_ = std::make_unique<modemfwd::ModemFlasher>(
      std::move(firmware_directory), std::move(journal));

  modem_tracker_ = std::make_unique<modemfwd::ModemTracker>(
      bus_,
      base::BindRepeating(&Daemon::OnModemCarrierIdReady,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(&Daemon::OnModemDeviceSeen,
                          weak_ptr_factory_.GetWeakPtr()));

  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&Daemon::CheckForWedgedModems,
                     weak_ptr_factory_.GetWeakPtr()),
      GetModemWedgeCheckDelay());

  return EX_OK;
}

int Daemon::OnEventLoopStarted() {
  // Stub until DLC service is implemented.
  return EX_OK;
}

void Daemon::OnModemDeviceSeen(std::string device_id,
                               std::string equipment_id) {
  ELOG(INFO) << "Modem seen with equipment ID \"" << equipment_id << "\""
             << " and device ID [" << device_id << "]";
  // Record that we've seen this modem so we don't reboot/auto-force-flash it.
  device_ids_seen_.insert(device_id);

  if (modem_reappear_callbacks_.count(equipment_id) > 0) {
    std::move(modem_reappear_callbacks_[equipment_id]).Run();
    modem_reappear_callbacks_.erase(equipment_id);
  }
}

void Daemon::OnModemCarrierIdReady(
    std::unique_ptr<org::chromium::flimflam::DeviceProxy> device) {
  auto modem = CreateModem(bus_, std::move(device), helper_directory_.get());
  if (!modem)
    return;

  std::string equipment_id = modem->GetEquipmentId();
  std::string device_id = modem->GetDeviceId();
  ELOG(INFO) << "Modem with equipment ID \"" << equipment_id << "\""
             << " and device ID [" << device_id << "] ready to flash";

  if (IsAutoUpdateDisabledByPref()) {
    LOG(INFO) << "Update disabled by pref";
    return;
  }

  base::OnceClosure cb = modem_flasher_->TryFlash(modem.get());
  if (!cb.is_null())
    modem_reappear_callbacks_[equipment_id] = std::move(cb);
}

void Daemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  dbus_adaptor_.reset(new DBusAdaptor(bus_, this));
  dbus_adaptor_->RegisterAsync(
      sequencer->GetHandler("RegisterAsync() failed", true));
}

bool Daemon::ForceFlash(const std::string& device_id) {
  auto stub_modem = CreateStubModem(device_id, "", helper_directory_.get());
  if (!stub_modem)
    return false;

  ELOG(INFO) << "Force-flashing modem with device ID [" << device_id << "]";
  base::OnceClosure cb = modem_flasher_->TryFlash(stub_modem.get());
  // We don't know the equipment ID of this modem, and if we're force-flashing
  // then we probably already have a problem with the modem coming up, so
  // cleaning up at this point is not a problem. Run the callback now if we
  // got one.
  bool is_null = cb.is_null();
  if (!is_null)
    std::move(cb).Run();
  return !is_null;
}

bool Daemon::ForceFlashForTesting(const std::string& device_id,
                                  const std::string& carrier_uuid,
                                  const std::string& variant) {
  auto stub_modem =
      CreateStubModem(device_id, carrier_uuid, helper_directory_.get());
  if (!stub_modem)
    return false;

  ELOG(INFO) << "Force-flashing modem with device ID [" << device_id << "], "
             << "variant [" << variant << "], carrier_uuid [" << carrier_uuid
             << "]";
  base::OnceClosure cb =
      modem_flasher_->TryFlashForTesting(stub_modem.get(), variant);
  // We don't know the equipment ID of this modem, and if we're force-flashing
  // then we probably already have a problem with the modem coming up, so
  // cleaning up at this point is not a problem. Run the callback now if we
  // got one.
  bool is_null = cb.is_null();
  if (!is_null)
    std::move(cb).Run();
  return !is_null;
}

void Daemon::CheckForWedgedModems() {
  EVLOG(1) << "Running wedged modems check...";
  helper_directory_->ForEachHelper(
      base::BindRepeating(&Daemon::ForceFlashIfWedged, base::Unretained(this)));
}

void Daemon::ForceFlashIfWedged(const std::string& device_id,
                                ModemHelper* helper) {
  if (device_ids_seen_.count(device_id) > 0)
    return;

  if (!helper->FlashModeCheck()) {
    LOG(WARNING) << "Modem not found, trying to reset it...";
    if (helper->Reboot()) {
      base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&Daemon::ForceFlashIfNeverAppeared,
                         weak_ptr_factory_.GetWeakPtr(), device_id),
          kRebootCheckDelay);
    } else {
      EVLOG(1) << "Couldn't reboot modem with device ID [" << device_id
               << "], it may not be present";
    }
    return;
  }

  LOG(INFO) << "Modem with device ID [" << device_id
            << "] appears to be wedged, attempting recovery";
  ForceFlash(device_id);
}

void Daemon::ForceFlashIfNeverAppeared(const std::string& device_id) {
  if (device_ids_seen_.count(device_id) > 0)
    return;

  LOG(INFO) << "Modem with device ID [" << device_id
            << "] did not appear after reboot, attempting recovery";
  ForceFlash(device_id);
}

}  // namespace modemfwd
