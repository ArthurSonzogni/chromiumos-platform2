// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/daemon.h"

#include <signal.h>
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
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/task/single_thread_task_runner.h>
#include <base/time/time.h>
#include <cros_config/cros_config.h>
#include <chromeos/dbus/service_constants.h>
#include <sys/wait.h>

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

bool DBusAdaptor::ForceFlash(const std::string& device_id,
                             const brillo::VariantDictionary& args) {
  std::string carrier_uuid =
      brillo::GetVariantValueOrDefault<std::string>(args, "carrier_uuid");
  std::string variant =
      brillo::GetVariantValueOrDefault<std::string>(args, "variant");
  bool use_modems_fw_info =
      brillo::GetVariantValueOrDefault<bool>(args, "use_modems_fw_info");
  return delegate_->ForceFlashForTesting(device_id, carrier_uuid, variant,
                                         use_modems_fw_info);
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
  if (exit_code != EX_OK)
    return exit_code;
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
  helper_directory_ =
      CreateModemHelperDirectory(helper_dir_path_, variant_, bus_);
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
  if (fw_manifest_dir_path_.empty())
    return EX_UNAVAILABLE;

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

  modem_flasher_ = std::make_unique<modemfwd::ModemFlasher>(
      this, fw_manifest_directory_.get(), journal_.get(),
      notification_mgr_.get(), metrics_.get());

  modem_tracker_ = std::make_unique<modemfwd::ModemTracker>(
      bus_,
      base::BindRepeating(&Daemon::OnModemCarrierIdReady,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(&Daemon::OnModemDeviceSeen,
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

void Daemon::OnModemDeviceSeen(std::string device_id,
                               std::string equipment_id) {
  ELOG(INFO) << "Modem seen with equipment ID \"" << equipment_id << "\""
             << " and device ID [" << device_id << "]";
  // Record that we've seen this modem so we don't reboot/auto-force-flash it.
  device_ids_seen_.insert(device_id);

  // The modem that matches the variant has been seen.
  if (fw_manifest_directory_->DeviceIdMatch(device_id) &&
      !modems_seen_since_oobe_prefs_->Exists(device_id)) {
    if (!modems_seen_since_oobe_prefs_->Create(device_id))
      LOG(ERROR) << "Failed to create modem seen pref for modem: " << device_id;
  }

  RunModemReappearanceCallback(equipment_id);
}

void Daemon::OnModemCarrierIdReady(
    std::unique_ptr<org::chromium::flimflam::DeviceProxyInterface> device) {
  auto modem =
      CreateModem(bus_.get(), std::move(device), helper_directory_.get());
  if (!modem)
    return;

  std::string device_id = modem->GetDeviceId();
  std::string equipment_id = modem->GetEquipmentId();

  // Store the modem object in case our flash gets delayed.
  modems_[device_id] = std::move(modem);
  SetupHeartbeatTask(device_id);

  ELOG(INFO) << "Modem with equipment ID \"" << equipment_id << "\""
             << " and device ID [" << device_id << "] ready to flash";

  if (prefs_->KeyValueMatches(kDisableAutoUpdateKey, "1")) {
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
  StopHeartbeatTask(device_id);
  brillo::ErrorPtr err;
  modem_flasher_->TryFlash(modems_[device_id].get(),
                           modems_seen_since_oobe_prefs_->Exists(device_id),
                           &err);
  StartHeartbeatTask(device_id);

  if (err) {
    LOG(ERROR) << "Flashing returned error: " << err->GetMessage();
  }
}

void Daemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  dbus_adaptor_.reset(new DBusAdaptor(bus_, this));
  dbus_adaptor_->RegisterAsync(
      sequencer->GetHandler("RegisterAsync() failed", true));
}

bool Daemon::ForceFlash(const std::string& device_id) {
  auto stub_modem =
      CreateStubModem(device_id, "", helper_directory_.get(), false);
  if (!stub_modem)
    return false;

  ELOG(INFO) << "Force-flashing modem with device ID [" << device_id << "]";
  StopHeartbeatTask(device_id);
  brillo::ErrorPtr err;
  modem_flasher_->TryFlash(
      stub_modem.get(), modems_seen_since_oobe_prefs_->Exists(device_id), &err);
  StartHeartbeatTask(device_id);

  // We don't know the real equipment ID of this modem, and if we're
  // force-flashing then we probably already have a problem with the modem
  // coming up, so cleaning up at this point is not a problem. Run the callback
  // now if we got one.
  RunModemReappearanceCallback(stub_modem->GetEquipmentId());
  return !err;
}

bool Daemon::ForceFlashForTesting(const std::string& device_id,
                                  const std::string& carrier_uuid,
                                  const std::string& variant,
                                  bool use_modems_fw_info) {
  // Just drop the request if we're suspending. Users can manually retry the
  // force-flash after the device has resumed.
  if (suspend_checker_->IsSuspendAnnounced())
    return false;

  auto stub_modem = CreateStubModem(
      device_id, carrier_uuid, helper_directory_.get(), use_modems_fw_info);
  if (!stub_modem)
    return false;

  ELOG(INFO) << "Force-flashing modem with device ID [" << device_id << "], "
             << "variant [" << variant << "], carrier_uuid [" << carrier_uuid
             << "], use_modems_fw_info [" << use_modems_fw_info << "]";

  fw_manifest_directory_->OverrideVariantForTesting(variant);

  StopHeartbeatTask(device_id);
  brillo::ErrorPtr err;
  modem_flasher_->TryFlash(
      stub_modem.get(), modems_seen_since_oobe_prefs_->Exists(device_id), &err);
  StartHeartbeatTask(device_id);

  // We don't know the real equipment ID of this modem, and if we're
  // force-flashing then we probably already have a problem with the modem
  // coming up, so cleaning up at this point is not a problem. Run the callback
  // now if we got one.
  RunModemReappearanceCallback(stub_modem->GetEquipmentId());
  return !err;
}

bool Daemon::ResetModem(const std::string& device_id) {
  auto helper = helper_directory_->GetHelperForDeviceId(device_id);
  if (!helper)
    return false;

  return helper->Reboot();
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

  if (!helper->FlashModeCheck()) {
    LOG(WARNING) << "Modem not found, trying to reset it...";
    if (helper->Reboot()) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&Daemon::ForceFlashIfNeverAppeared,
                         weak_ptr_factory_.GetWeakPtr(), device_id),
          kRebootCheckDelay);
    } else {
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
    return;
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

void Daemon::SetupHeartbeatTask(const std::string& device_id) {
  heartbeat_tasks_.erase(device_id);

  if (!modems_[device_id]->SupportsHealthCheck())
    return;

  // This modem has a port we can run health checks on. See if we need to
  // set it up.
  auto helper = helper_directory_->GetHelperForDeviceId(device_id);
  if (!helper)
    return;

  auto heartbeat_config = helper->GetHeartbeatConfig();
  if (!heartbeat_config.has_value())
    return;

  // We support heartbeat checks on this modem. Create a task to do these
  // on a recurring basis.
  auto heartbeat_task = std::make_unique<HeartbeatTask>(
      this, modems_[device_id].get(), metrics_.get(), *heartbeat_config);
  heartbeat_tasks_[device_id] = std::move(heartbeat_task);
}

void Daemon::StartHeartbeatTask(const std::string& device_id) {
  auto it = heartbeat_tasks_.find(device_id);
  if (it == heartbeat_tasks_.end())
    return;

  it->second->Start();
}

void Daemon::StopHeartbeatTask(const std::string& device_id) {
  auto it = heartbeat_tasks_.find(device_id);
  if (it == heartbeat_tasks_.end())
    return;

  it->second->Stop();
}

}  // namespace modemfwd
