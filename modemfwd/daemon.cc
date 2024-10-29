// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/daemon.h"

#include <signal.h>
#include <sys/wait.h>
#include <sysexits.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/containers/contains.h>
#include <base/containers/fixed_flat_map.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/memory/scoped_refptr.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/task/single_thread_task_runner.h>
#include <base/time/time.h>
#include <chromeos/dbus/service_constants.h>
#include <cros_config/cros_config.h>

#include "modemfwd/dlc_manager.h"
#include "modemfwd/error.h"
#include "modemfwd/firmware_directory.h"
#include "modemfwd/logging.h"
#include "modemfwd/metrics.h"
#include "modemfwd/modem.h"
#include "modemfwd/modem_flasher.h"
#include "modemfwd/modem_helper_directory.h"
#include "modemfwd/modem_sandbox.h"
#include "modemfwd/modem_tracker.h"
#include "modemfwd/notification_manager.h"
#include "modemfwd/prefs.h"
#include "modemfwd/proto_bindings/firmware_manifest_v2.pb.h"

namespace {

const char kManifestName[] = "firmware_manifest.textproto";
const char kManifestNameLegacy[] = "firmware_manifest.prototxt";
constexpr base::TimeDelta kWedgeCheckDelay = base::Minutes(2);
constexpr base::TimeDelta kRebootCheckDelay = base::Minutes(1);
constexpr base::TimeDelta kDlcRemovalDelay = base::Minutes(2);
constexpr base::TimeDelta kPowerOffHystTime = base::Minutes(1);

constexpr char kPrefsDir[] = "/var/lib/modemfwd/";
// The existence of a device id in |kModemsSeenSinceOobeKey| is used to
// indicate if a modem that belongs to that variant was ever seen.
constexpr char kModemsSeenSinceOobeKey[] = "modems_seen_since_oobe";
constexpr char kDisableAutoUpdateKey[] = "disable_auto_update";

// Returns the modem firmware variant for the current model of the device by
// reading the /modem/firmware-variant property of the current model via
// chromeos-config. Returns an empty string if it fails to read the modem
// firmware variant from chromeos-config or no modem firmware variant is
// specified.
std::string GetModemFirmwareVariant() {
  brillo::CrosConfig config;
  std::string variant;
  if (!config.GetString("/modem", "firmware-variant", &variant)) {
    LOG(INFO) << "No modem firmware variant is specified";
    return std::string();
  }

  LOG(INFO) << "Use modem firmware variant: " << variant;
  return variant;
}

std::string ToOnOffString(bool b) {
  return b ? "on" : "off";
}

// Returns the delay to wait before rebooting the modem if it hasn't appeared
// on the USB bus by reading the /modem/wedge-reboot-delay-ms property of the
// current model via chromeos-config, or using the default `kWedgeCheckDelay`
// constant if it fails to read it from chromeos-config or nothing is specified.
base::TimeDelta GetModemWedgeCheckDelay() {
  brillo::CrosConfig config;
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

  base::TimeDelta wedge_delay = base::Milliseconds(ms);
  LOG(INFO) << "Use customized wedge reboot delay: " << wedge_delay;
  return wedge_delay;
}
}  // namespace

namespace modemfwd {

DBusAdaptor::DBusAdaptor(scoped_refptr<dbus::Bus> bus, Delegate* delegate)
    : org::chromium::ModemfwdAdaptor(this),
      dbus_object_(nullptr, bus, dbus::ObjectPath(kModemfwdServicePath)),
      delegate_(delegate) {
  DCHECK(delegate);
}

void DBusAdaptor::RegisterAsync(
    brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(std::move(cb));
}

void DBusAdaptor::SetDebugMode(bool debug_mode) {
  g_extra_logging = debug_mode;
  LOG(INFO) << "Debug mode is now " << ToOnOffString(ELOG_IS_ON());
}

void DBusAdaptor::ForceFlash(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> resp,
    const std::string& device_id,
    const brillo::VariantDictionary& args) {
  std::string carrier_uuid =
      brillo::GetVariantValueOrDefault<std::string>(args, "carrier_uuid");
  std::string variant =
      brillo::GetVariantValueOrDefault<std::string>(args, "variant");
  bool use_modems_fw_info =
      brillo::GetVariantValueOrDefault<bool>(args, "use_modems_fw_info");

  auto callback = base::BindOnce(
      [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> resp,
         const brillo::ErrorPtr& error) {
        // This can be modified to return the actual brillo error.
        resp->Return(!error);
      },
      std::move(resp));
  return delegate_->ForceFlashForTesting(device_id, carrier_uuid, variant,
                                         use_modems_fw_info,
                                         std::move(callback));
}

Daemon::Daemon(const std::string& journal_file,
               const std::string& helper_directory,
               const std::string& firmware_directory)
    : DBusServiceDaemon(kModemfwdServiceName),
      journal_file_path_(journal_file),
      helper_dir_path_(helper_directory),
      fw_manifest_dir_path_(firmware_directory),
      weak_ptr_factory_(this) {}

int Daemon::OnInit() {
  int exit_code = brillo::DBusServiceDaemon::OnInit();
  if (exit_code != EX_OK) {
    return exit_code;
  }
  DCHECK(!helper_dir_path_.empty());

  std::unique_ptr<MetricsLibraryInterface> metrics_library =
      std::make_unique<MetricsLibrary>();
  metrics_ = std::make_unique<Metrics>(std::move(metrics_library));

  notification_mgr_ = std::make_unique<NotificationManager>(dbus_adaptor_.get(),
                                                            metrics_.get());
  if (!base::DirectoryExists(helper_dir_path_)) {
    auto err = Error::Create(
        FROM_HERE, kErrorResultInitFailure,
        base::StringPrintf(
            "Supplied modem-specific helper directory %s does not exist",
            helper_dir_path_.value().c_str()));
    notification_mgr_->NotifyUpdateFirmwareCompletedFailure(err.get());
    return EX_UNAVAILABLE;
  }

  prefs_ = Prefs::CreatePrefs(base::FilePath(kPrefsDir));
  if (!prefs_) {
    auto err = Error::Create(FROM_HERE, kErrorResultInitFailure,
                             "Prefs could not be created");
    notification_mgr_->NotifyUpdateFirmwareCompletedFailure(err.get());
    return EX_UNAVAILABLE;
  }
  modems_seen_since_oobe_prefs_ =
      Prefs::CreatePrefs(*prefs_, kModemsSeenSinceOobeKey);
  if (!modems_seen_since_oobe_prefs_) {
    auto err = Error::Create(FROM_HERE, kErrorResultInitFailure,
                             "ModemsSeenSinceOobe prefs could not be created");
    notification_mgr_->NotifyUpdateFirmwareCompletedFailure(err.get());
    return EX_UNAVAILABLE;
  }

  variant_ = GetModemFirmwareVariant();
  helper_directory_ = CreateModemHelperDirectory(helper_dir_path_, variant_);
  if (!helper_directory_) {
    auto err =
        Error::Create(FROM_HERE,
                      (variant_.empty() ? kErrorResultInitFailureNonLteSku
                                        : kErrorResultInitFailure),
                      base::StringPrintf("No suitable helpers found in %s",
                                         helper_dir_path_.value().c_str()));
    notification_mgr_->NotifyUpdateFirmwareCompletedFailure(err.get());
    return EX_UNAVAILABLE;
  }

  // If no firmware directory was supplied, we can't run.
  if (fw_manifest_dir_path_.empty()) {
    return EX_UNAVAILABLE;
  }

  if (!base::DirectoryExists(fw_manifest_dir_path_)) {
    auto err = Error::Create(
        FROM_HERE, kErrorResultInitFailure,
        base::StringPrintf("Supplied firmware directory %s does not exist",
                           fw_manifest_dir_path_.value().c_str()));
    notification_mgr_->NotifyUpdateFirmwareCompletedFailure(err.get());
    return EX_UNAVAILABLE;
  }

  suspend_checker_ = SuspendChecker::Create();
  if (!suspend_checker_) {
    auto err = Error::Create(FROM_HERE, kErrorResultInitFailure,
                             "Suspend checker could not be created");
    notification_mgr_->NotifyUpdateFirmwareCompletedFailure(err.get());
    return EX_UNAVAILABLE;
  }

  return SetupFirmwareDirectory();
}

int Daemon::SetupFirmwareDirectory() {
  CHECK(!fw_manifest_dir_path_.empty());

  std::map<std::string, Dlc> dlc_per_variant;
  auto file_name = base::PathExists(fw_manifest_dir_path_.Append(kManifestName))
                       ? kManifestName
                       : kManifestNameLegacy;
  fw_index_ = ParseFirmwareManifestV2(fw_manifest_dir_path_.Append(file_name),
                                      dlc_per_variant);
  if (!fw_index_) {
    auto err = Error::Create(
        FROM_HERE, kErrorResultInitManifestFailure,
        "Could not load firmware manifest directory (bad manifest?)");
    notification_mgr_->NotifyUpdateFirmwareCompletedFailure(err.get());
    return EX_UNAVAILABLE;
  }

  if (!dlc_per_variant.empty()) {
    LOG(INFO) << "Creating DLC manager";
    dlc_manager_ = std::make_unique<modemfwd::DlcManager>(
        bus_, metrics_.get(), std::move(dlc_per_variant), variant_);
    if (dlc_manager_->DlcId().empty()) {
      LOG(ERROR) << "Unexpected empty DlcId value";
      auto err = Error::Create(FROM_HERE, error::kUnexpectedEmptyDlcId,
                               "Unexpected empty DlcId value");
      metrics_->SendDlcInstallResultFailure(err.get());
    } else {
      InstallModemDlcOnceCallback cb = base::BindOnce(
          &Daemon::InstallDlcCompleted, weak_ptr_factory_.GetWeakPtr());
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(&DlcManager::InstallModemDlc,
                         base::Unretained(dlc_manager_.get()), std::move(cb)));
      return EX_OK;
    }
  }
  metrics_->SendFwUpdateLocation(metrics::FwUpdateLocation::kRootFS);
  CompleteInitialization();
  return EX_OK;
}

void Daemon::InstallDlcCompleted(const std::string& mount_path,
                                 const brillo::Error* error) {
  if (error || mount_path.empty()) {
    LOG(INFO) << "Failed to install DLC. Falling back to rootfs";
    metrics_->SendFwUpdateLocation(
        metrics::FwUpdateLocation::kFallbackToRootFS);
    CompleteInitialization();
    return;
  }

  if (dlc_manager_->IsDlcEmpty()) {
    LOG(INFO) << "Ignoring DLC contents, loading FW from rootfs";
    metrics_->SendFwUpdateLocation(metrics::FwUpdateLocation::kRootFS);
  } else {
    fw_manifest_directory_ = CreateFirmwareDirectory(
        std::move(fw_index_), base::FilePath(mount_path), variant_);
    metrics_->SendFwUpdateLocation(metrics::FwUpdateLocation::kDlc);
  }
  CompleteInitialization();
}

void Daemon::CompleteInitialization() {
  if (!fw_manifest_directory_) {
    fw_manifest_directory_ = CreateFirmwareDirectory(
        std::move(fw_index_), fw_manifest_dir_path_, variant_);
  }
  DCHECK(fw_manifest_directory_);

  journal_ = OpenJournal(journal_file_path_, fw_manifest_directory_.get(),
                         helper_directory_.get());
  if (!journal_) {
    auto err = Error::Create(FROM_HERE, kErrorResultInitJournalFailure,
                             "Could not open journal file");
    notification_mgr_->NotifyUpdateFirmwareCompletedFailure(err.get());
    QuitWithExitCode(EX_UNAVAILABLE);
  }

  auto modem_flasher = CreateModemFlasher(fw_manifest_directory_.get(),
                                          modems_seen_since_oobe_prefs_.get());
  async_modem_flasher_ =
      base::MakeRefCounted<AsyncModemFlasher>(std::move(modem_flasher));

  modem_tracker_ = std::make_unique<modemfwd::ModemTracker>(
      bus_,
      base::BindRepeating(&Daemon::OnModemCarrierIdReady,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(&Daemon::OnModemDeviceSeen,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(&Daemon::OnModemStateChange,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(&Daemon::OnModemPowerStateChange,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(&Daemon::OnPoweredChange,
                          weak_ptr_factory_.GetWeakPtr()));

  if (dlc_manager_) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&DlcManager::RemoveUnecessaryModemDlcs,
                       base::Unretained(dlc_manager_.get())),
        kDlcRemovalDelay);
  }

  // Check if we have any qcom soc based modems that require a flash before they
  // boot.
  const char kSocInternalDeviceId[] = "soc:*:* (Internal)";
  if (helper_directory_->GetHelperForDeviceId(kSocInternalDeviceId)) {
    ForceFlash(kSocInternalDeviceId);
  } else {
    helper_directory_->ForEachHelper(base::BindRepeating(
        &Daemon::ForceFlashIfInFlashMode, base::Unretained(this)));
  }

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&Daemon::CheckForWedgedModems,
                     weak_ptr_factory_.GetWeakPtr()),
      GetModemWedgeCheckDelay());
}

void Daemon::RegisterOnStartFlashingCallback(const std::string& equipment_id,
                                             base::OnceClosure callback) {
  start_flashing_callbacks_[equipment_id].push_back(std::move(callback));
}

void Daemon::RegisterOnModemReappearanceCallback(
    const std::string& equipment_id, base::OnceClosure callback) {
  modem_reappear_callbacks_[equipment_id] = std::move(callback);
}

void Daemon::RunModemReappearanceCallback(const std::string& equipment_id) {
  if (modem_reappear_callbacks_.count(equipment_id) > 0) {
    std::move(modem_reappear_callbacks_[equipment_id]).Run();
    modem_reappear_callbacks_.erase(equipment_id);
  }
}

void Daemon::RegisterOnModemStateChangedCallback(
    const std::string& device_id, base::RepeatingClosure callback) {
  state_change_callbacks_[device_id].push_back(std::move(callback));
}

void Daemon::RegisterOnModemPowerStateChangedCallback(
    const std::string& device_id, base::RepeatingClosure callback) {
  power_state_change_callbacks_[device_id].push_back(std::move(callback));
}

void Daemon::OnModemStateChange(std::string device_id, Modem::State new_state) {
  if (modems_.count(device_id) == 0) {
    return;
  }
  EVLOG(1) << __func__ << ": update modem with device id: " << device_id
           << " to new modem state: " << new_state;
  // Do not update heartbeat config when:
  // 1. update to new modem state is not successful (no state change);
  // 2. current power state is LOW, keep heartbeat stopped.
  if (!modems_[device_id]->UpdateState(new_state)) {
    return;
  }
  for (const auto& cb : state_change_callbacks_[device_id]) {
    cb.Run();
  }
}

void Daemon::OnModemPowerStateChange(std::string device_id,
                                     Modem::PowerState new_power_state) {
  if (modems_.count(device_id) == 0) {
    return;
  }
  EVLOG(1) << __func__ << ": update modem with device id: " << device_id
           << " to new power state: " << new_power_state;
  if (!modems_[device_id]->UpdatePowerState(new_power_state)) {
    return;
  }
  for (const auto& cb : power_state_change_callbacks_[device_id]) {
    cb.Run();
  }
}

void Daemon::OnPoweredChange(const std::string device_id, bool powered) {
  // Do not change modem power state during suspension.
  if (suspend_checker_->IsSuspendAnnounced()) {
    return;
  }
  std::string power_device_id = device_id;
  // Once modem is powered off, the device_id of the cellular device is empty
  if (powered && power_device_id.empty()) {
    for (const auto& modem : modems_) {
      if (modem.second->GetPowerState() == Modem::PowerState::OFF ||
          // power state is unknown if modemfwd has restarted with modem off
          modem.second->GetPowerState() == Modem::PowerState::UNKNOWN) {
        power_device_id = modem.first;
        break;
      }
    }
  }
  if (modems_.count(power_device_id) == 0) {
    return;
  }
  if (powered) {
    modems_[power_device_id]->UpdatePowerOffPendingFlag(false);
    ChangeModemPowerState(power_device_id, Modem::PowerState::ON);
  } else {
    // post delayed task (hysteresis timer) for modem power off
    if (!modems_[power_device_id]->IsPowerOffPending()) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&Daemon::ModemPowerOff, weak_ptr_factory_.GetWeakPtr(),
                         power_device_id),
          kPowerOffHystTime);
      modems_[power_device_id]->UpdatePowerOffPendingFlag(true);
    }
  }
}

void Daemon::OnModemDeviceSeen(std::string device_id,
                               std::string equipment_id) {
  ELOG(INFO) << "Modem seen with equipment ID \"" << equipment_id << "\""
             << " and device ID [" << device_id << "]";
  // Record that we've seen this modem so we don't reboot/auto-force-flash it.
  device_ids_seen_.insert(device_id);

  // The modem that matches the variant has been seen.
  if (fw_manifest_directory_->DeviceIdMatch(device_id) &&
      !modems_seen_since_oobe_prefs_->Exists(device_id)) {
    if (!modems_seen_since_oobe_prefs_->Create(device_id)) {
      LOG(ERROR) << "Failed to create modem seen pref for modem: " << device_id;
    }
  }

  RunModemReappearanceCallback(equipment_id);
}

void Daemon::OnModemCarrierIdReady(
    std::unique_ptr<org::chromium::flimflam::DeviceProxyInterface> device) {
  auto modem =
      CreateModem(bus_.get(), std::move(device), helper_directory_.get());
  if (!modem) {
    return;
  }

  std::string device_id = modem->GetDeviceId();
  std::string equipment_id = modem->GetEquipmentId();

  state_change_callbacks_.erase(device_id);
  power_state_change_callbacks_.erase(device_id);

  auto heartbeat_task = HeartbeatTask::Create(
      this, modem.get(), helper_directory_.get(), metrics_.get());
  if (heartbeat_task) {
    HeartbeatTask* weak_task = heartbeat_task.get();
    AddTask(std::move(heartbeat_task));
    weak_task->Start();
  }

  // Store the modem object now in case our flash gets delayed.
  modems_[device_id] = std::move(modem);

  ELOG(INFO) << "Modem with equipment ID \"" << equipment_id << "\""
             << " and device ID [" << device_id << "] ready to flash";

  if (prefs_->Exists(kDisableAutoUpdateKey) &&
      prefs_->KeyValueMatches(kDisableAutoUpdateKey, "1")) {
    LOG(INFO) << "Update disabled by pref";
    notification_mgr_->NotifyUpdateFirmwareCompletedSuccess(false, 0);
    return;
  }

  suspend_checker_->RunWhenNotSuspending(
      base::BindOnce(&Daemon::DoFlash, weak_ptr_factory_.GetWeakPtr(),
                     device_id, equipment_id));
}

void Daemon::DoFlash(const std::string& device_id,
                     const std::string& equipment_id) {
  brillo::ErrorPtr err;
  auto flash_task =
      std::make_unique<FlashTask>(this, journal_.get(), notification_mgr_.get(),
                                  metrics_.get(), bus_, async_modem_flasher_);
  FlashTask* weak_task = flash_task.get();

  AddTask(std::move(flash_task));
  weak_task->Start(modems_[device_id].get(), FlashTask::Options{});
}

void Daemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  dbus_adaptor_.reset(new DBusAdaptor(bus_, this));
  dbus_adaptor_->RegisterAsync(
      sequencer->GetHandler("RegisterAsync() failed", true));
}

void Daemon::ForceFlash(const std::string& device_id) {
  ForceFlashForTesting(device_id, "", "", false, base::DoNothing());
}

void Daemon::ForceFlashForTesting(
    const std::string& device_id,
    const std::string& carrier_uuid,
    const std::string& variant,
    bool use_modems_fw_info,
    base::OnceCallback<void(const brillo::ErrorPtr&)> callback) {
  // Just drop the request if we're suspending. Users can manually retry the
  // force-flash after the device has resumed.
  if (suspend_checker_->IsSuspendAnnounced()) {
    std::move(callback).Run(Error::Create(FROM_HERE, kErrorResultFailure,
                                          "Device is suspending, retry later"));
    return;
  }

  auto stub_modem =
      CreateStubModem(device_id, helper_directory_.get(), use_modems_fw_info);
  if (!stub_modem) {
    std::move(callback).Run(Error::Create(FROM_HERE, kErrorResultFailure,
                                          "Could not create stub modem"));
    return;
  }

  ELOG(INFO) << "Force-flashing modem with device ID [" << device_id << "]"
             << (variant.empty() ? "" : ", variant [" + variant + "]")
             << (carrier_uuid.empty() ? ""
                                      : ", carrier_uuid [" + carrier_uuid + "]")
             << (use_modems_fw_info ? " using real modem firmware info" : "");

  fw_manifest_directory_->OverrideVariantForTesting(variant);

  brillo::ErrorPtr err;
  auto flash_task =
      std::make_unique<FlashTask>(this, journal_.get(), notification_mgr_.get(),
                                  metrics_.get(), bus_, async_modem_flasher_);
  FlashTask* flash_task_ptr = flash_task.get();
  AddTask(std::move(flash_task));

  FlashTask::Options opts;
  opts.should_always_flash = true;
  if (!carrier_uuid.empty()) {
    opts.carrier_override_uuid = carrier_uuid;
  }

  Modem* weak_modem = stub_modem.get();
  RegisterOnTaskFinishedCallback(
      flash_task_ptr,
      base::BindOnce(
          [](base::OnceCallback<void(const brillo::ErrorPtr&)> callback,
             std::unique_ptr<Modem> modem, const brillo::ErrorPtr& error) {
            std::move(callback).Run(error);
            // Don't clean up the modem until this task is fully finished.
            modem.reset();
          },
          std::move(callback), std::move(stub_modem)));
  flash_task_ptr->Start(weak_modem, opts);
}

bool Daemon::ResetModem(const std::string& device_id) {
  auto helper = helper_directory_->GetHelperForDeviceId(device_id);
  if (!helper) {
    return false;
  }

  return helper->Reboot();
}

void Daemon::NotifyFlashStarting(const std::string& equipment_id) {
  if (start_flashing_callbacks_.count(equipment_id) > 0) {
    for (auto& cb : start_flashing_callbacks_[equipment_id]) {
      std::move(cb).Run();
    }
    start_flashing_callbacks_.erase(equipment_id);
  }
}

bool Daemon::ChangeModemPowerState(const std::string& device_id,
                                   Modem::PowerState target_state) {
  auto helper = helper_directory_->GetHelperForDeviceId(device_id);
  bool res = true;
  if (!helper) {
    return false;
  }
  if (modems_.count(device_id) == 0) {
    return false;
  }
  if (target_state == Modem::PowerState::ON) {
    res = helper->PowerOn();
    // Do not update power state here; wait for MM to report
  } else if (target_state == Modem::PowerState::OFF &&
             // Modem has to be stopped before powering off
             (modems_[device_id]->GetPowerState() == Modem::PowerState::LOW ||
              modems_[device_id]->GetPowerState() ==
                  Modem::PowerState::UNKNOWN)) {
    if (helper->PowerOff()) {
      modems_[device_id]->UpdatePowerState(Modem::PowerState::OFF);
      res = true;
    } else {
      res = false;
    }
  } else {
    EVLOG(1) << __func__ << "power state: " << target_state << " not handled";
    res = false;
  }
  ELOG(INFO) << __func__ << ": change modem with device id: " << device_id
             << " to new power state: " << target_state << " result: " << res;
  return res;
}

void Daemon::ModemPowerOff(const std::string& device_id) {
  if (modems_.count(device_id) == 0) {
    return;
  }
  for (const auto& [name, full_task] : tasks_) {
    if (full_task.task->type() == kTaskTypeFlash) {
      // Delay powering off the modem when flashing is ongoing
      // TODO(b/372748517): remove this delay and retry when requests to
      // the modem helpers are serialized.
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&Daemon::ModemPowerOff, weak_ptr_factory_.GetWeakPtr(),
                         device_id),
          kPowerOffHystTime);
      return;
    }
  }
  if (modems_[device_id]->IsPowerOffPending()) {
    modems_[device_id]->UpdatePowerOffPendingFlag(false);
    ChangeModemPowerState(device_id, Modem::PowerState::OFF);
  }
}

void Daemon::ForceFlashIfInFlashMode(const std::string& device_id,
                                     ModemHelper* helper) {
  EVLOG(1) << __func__ << "device_id: " << device_id;
  if (!helper->FlashModeCheck()) {
    return;
  }

  metrics_->SendCheckForWedgedModemResult(
      metrics::CheckForWedgedModemResult::kModemWedged);
  LOG(INFO) << "Modem with device ID [" << device_id
            << "] appears to be in flash mode, attempting recovery";
  ForceFlash(device_id);
}

void Daemon::CheckForWedgedModems() {
  EVLOG(1) << "Running wedged modems check...";

  helper_directory_->ForEachHelper(
      base::BindRepeating(&Daemon::ForceFlashIfWedged, base::Unretained(this)));
}

void Daemon::ForceFlashIfWedged(const std::string& device_id,
                                ModemHelper* helper) {
  if (device_ids_seen_.count(device_id) > 0) {
    metrics_->SendCheckForWedgedModemResult(
        metrics::CheckForWedgedModemResult::kModemPresent);
    return;
  }

  // Skip when the modem is turned off
  if (modems_.count(device_id) &&
      modems_[device_id]->GetPowerState() == Modem::PowerState::OFF) {
    return;
  }

  // Check if the modem is in flash mode (fastboot or download mode).
  //  -> If yes, proceed directly to force flash.
  // For modems that don't support flash mode check (e.g., FM350)
  // or are not detected on the bus:
  //  -> Attempt to recover by rebooting the modem using modem-helper.
  //  -> If the reboot is successful:
  //     -> Wait for a minute for the modem to appear on the bus.
  //     -> Then, force-flash the modem.
  // If the helper-based reboot fails:
  //  -> Try force-flashing the modem.
  if (!helper->FlashModeCheck()) {
    LOG(WARNING) << "Modem not found, trying to reset it...";
    if (helper->Reboot()) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&Daemon::ForceFlashIfNeverAppeared,
                         weak_ptr_factory_.GetWeakPtr(), device_id),
          kRebootCheckDelay);
      return;
    }
    EVLOG(1) << "Couldn't reboot modem with device ID [" << device_id
             << "], it may not be present";
    // |kFailedToRebootModem| will be sent only on devices with a modem
    // firmware-variant, since devices without a modem will always fail to
    // reboot the non existing modem and will pollute the metrics.
    if (!variant_.empty()) {
      metrics_->SendCheckForWedgedModemResult(
          metrics::CheckForWedgedModemResult::kFailedToRebootModem);
    }
  }

  metrics_->SendCheckForWedgedModemResult(
      metrics::CheckForWedgedModemResult::kModemWedged);
  LOG(INFO) << "Modem with device ID [" << device_id
            << "] appears to be wedged, attempting recovery";
  ForceFlash(device_id);
}

void Daemon::ForceFlashIfNeverAppeared(const std::string& device_id) {
  if (device_ids_seen_.count(device_id) > 0) {
    metrics_->SendCheckForWedgedModemResult(
        metrics::CheckForWedgedModemResult::kModemPresentAfterReboot);
    return;
  }

  LOG(INFO) << "Modem with device ID [" << device_id
            << "] did not appear after reboot, attempting recovery";
  metrics_->SendCheckForWedgedModemResult(
      metrics::CheckForWedgedModemResult::kModemAbsentAfterReboot);
  ForceFlash(device_id);
}

void Daemon::TaskUpdated(Task* /* task */) {
  std::vector<brillo::VariantDictionary> all_tasks;
  for (const auto& [name, full_task] : tasks_) {
    const std::unique_ptr<Task>& task = full_task.task;
    brillo::VariantDictionary task_props;
    task_props[kTaskName] = name;
    task_props[kTaskType] = task->type();
    task_props[kTaskStartedAt] =
        task->started_at().InMillisecondsSinceUnixEpoch();
    for (const auto& [key, value] : task->props()) {
      // Use emplace instead of operator[] so we don't overwrite any of the keys
      // above.
      task_props.emplace(key, value);
    }
    all_tasks.push_back(task_props);
  }
  dbus_adaptor_->SetInProgressTasks(all_tasks);
}

void Daemon::AddTask(std::unique_ptr<Task> task) {
  Task* weak_task = task.get();
  const std::string& name = task->name();
  tasks_[name] = TaskWithMetadata{std::move(task)};
  TaskUpdated(weak_task);
}

void Daemon::RegisterOnTaskFinishedCallback(
    Task* task, base::OnceCallback<void(const brillo::ErrorPtr&)> callback) {
  auto it = tasks_.find(task->name());
  if (it == tasks_.end()) {
    ELOG(WARNING) << "Tried to register a callback for untracked task "
                  << task->name();
    return;
  }

  TaskWithMetadata& full_task = it->second;
  full_task.finished_callbacks.push_back(std::move(callback));
}

void Daemon::FinishTask(Task* task, brillo::ErrorPtr error) {
  auto it = tasks_.find(task->name());
  if (it == tasks_.end()) {
    ELOG(WARNING) << "Task " << task->name() << " signaled it was finished "
                  << "but no such task was found in the task list";
    return;
  }

  if (error) {
    LOG(ERROR) << "Task " << task->name()
               << " finished with an error: " << error->GetMessage();
  }

  TaskWithMetadata& full_task = it->second;
  for (auto& cb : full_task.finished_callbacks) {
    std::move(cb).Run(error);
  }

  // Clean up the task by removing it from the task list and destroying
  // it async. (We do this to avoid potential issues if a task includes some
  // code after the Finish call.)
  base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(
      FROM_HERE, std::move(full_task.task));
  tasks_.erase(it);
  TaskUpdated(nullptr);
}

}  // namespace modemfwd
