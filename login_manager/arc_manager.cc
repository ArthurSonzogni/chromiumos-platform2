// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/arc_manager.h"

#include <string>
#include <utility>
#include <vector>

#include <arc/arc.pb.h>
#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/memory/raw_ref.h>
#include <base/types/expected.h>
#include <brillo/dbus/dbus_method_response.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/dbus/utils.h>
#include <brillo/errors/error.h>
#include <dbus/arc_manager/dbus-constants.h>
#include <dbus/bootlockbox/dbus-constants.h>
#include <dbus/bus.h>
#include <dbus/debugd/dbus-constants.h>
#include <dbus/error.h>
#include <dbus/login_manager/dbus-constants.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>

#include "login_manager/android_oci_wrapper.h"
#include "login_manager/arc_sideload_status_interface.h"
#include "login_manager/container_manager_interface.h"
#include "login_manager/dbus_util.h"
#include "login_manager/init_daemon_controller.h"
#include "login_manager/system_utils.h"
#include "login_manager/systemd_unit_starter.h"
#include "login_manager/upstart_signal_emitter.h"
#include "login_manager/validator_utils.h"

#if USE_ARC_ADB_SIDELOADING
#include "login_manager/arc_sideload_status.h"
#else
#include "login_manager/arc_sideload_status_stub.h"
#endif

namespace login_manager {
namespace {

constexpr char kLoggedInFlag[] = "/run/session_manager/logged_in";

// The only path where containers are allowed to be installed.  They must be
// part of the read-only, signed root image.
constexpr char kContainerInstallDirectory[] = "/opt/google/containers";

// Because the cheets logs are huge, we set the D-Bus timeout to 1 minute.
constexpr base::TimeDelta kBackupArcBugReportTimeout = base::Minutes(1);

#if USE_CHEETS
// To launch ARC, certain amount of free disk space is needed.
// Path and the amount for the check.
constexpr char kArcDiskCheckPath[] = "/home";
constexpr int64_t kArcCriticalDiskFreeBytes = 64 << 20;  // 64MB

// Workaround for a presubmit check about long lines.
constexpr auto
    kStartArcMiniInstanceRequest_DalvikMemoryProfile_MEM_PROFILE_DEFAULT = arc::
        StartArcMiniInstanceRequest_DalvikMemoryProfile_MEMORY_PROFILE_DEFAULT;

constexpr auto kStartArcMiniInstanceRequest_HostUreadaheadMode_MODE_DEFAULT =
    arc::StartArcMiniInstanceRequest_HostUreadaheadMode_MODE_DEFAULT;

// To set the CPU limits of the Android container.
constexpr char kCpuSharesFile[] =
    "/sys/fs/cgroup/cpu/session_manager_containers/cpu.shares";
constexpr unsigned int kCpuSharesForeground = 1024;
constexpr unsigned int kCpuSharesBackground = 64;

bool IsDevMode(SystemUtils& system_utils) {
  // When GetDevModeState() returns UNKNOWN, return true.
  return system_utils.GetDevModeState() != DevModeState::DEV_MODE_OFF;
}

bool IsInsideVm(SystemUtils& system_utils) {
  // When GetVmState() returns UNKNOWN, return false.
  return system_utils.GetVmState() == VmState::INSIDE_VM;
}

#endif

#if USE_SYSTEMD
using InitDaemonControllerImpl = SystemdUnitStarter;
#else
using InitDaemonControllerImpl = UpstartSignalEmitter;
#endif

std::unique_ptr<ArcSideloadStatusInterface> CreateArcSideloadStatus(
    dbus::Bus& bus) {
#if USE_ARC_ADB_SIDELOADING
  auto* boot_lockbox_dbus_proxy = bus.GetObjectProxy(
      bootlockbox::kBootLockboxServiceName,
      dbus::ObjectPath(bootlockbox::kBootLockboxServicePath));
  return std::make_unique<ArcSideloadStatus>(boot_lockbox_dbus_proxy);
#else
  return std::make_unique<ArcSideloadStatusStub>();
#endif
}

}  // namespace

class ArcManager::DBusService {
 public:
  explicit DBusService(org::chromium::ArcManagerAdaptor& adaptor)
      : adaptor_(adaptor) {}

  bool Start(const scoped_refptr<dbus::Bus>& bus) {
    CHECK(!dbus_object_);

    dbus_object_ = std::make_unique<brillo::dbus_utils::DBusObject>(
        /*object_manager=*/nullptr, bus,
        org::chromium::ArcManagerAdaptor::GetObjectPath(),
        /*property_handler_setup_callback=*/base::DoNothing());
    adaptor_->RegisterWithDBusObject(dbus_object_.get());
    dbus_object_->RegisterAndBlock();

    // Note: this needs to happen *after* all methods are exported.
    return bus->RequestOwnershipAndBlock(arc_manager::kArcManagerServiceName,
                                         dbus::Bus::REQUIRE_PRIMARY);
  }

 private:
  const raw_ref<org::chromium::ArcManagerAdaptor> adaptor_;
  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;
};

ArcManager::ArcManager(SystemUtils& system_utils,
                       LoginMetrics& login_metrics,
                       brillo::ProcessReaper& process_reaper,
                       scoped_refptr<dbus::Bus> bus)
    : ArcManager(
          system_utils,
          login_metrics,
          bus,
          std::make_unique<InitDaemonControllerImpl>(bus->GetObjectProxy(
              InitDaemonControllerImpl::kServiceName,
              dbus::ObjectPath(InitDaemonControllerImpl::kPath))),
          bus->GetObjectProxy(debugd::kDebugdServiceName,
                              dbus::ObjectPath(debugd::kDebugdServicePath)),
          std::make_unique<AndroidOciWrapper>(
              &system_utils,
              process_reaper,
              base::FilePath(kContainerInstallDirectory)),
          CreateArcSideloadStatus(*bus)) {}

ArcManager::ArcManager(
    SystemUtils& system_utils,
    LoginMetrics& login_metrics,
    scoped_refptr<dbus::Bus> bus,
    std::unique_ptr<InitDaemonController> init_controller,
    dbus::ObjectProxy* debugd_proxy,
    std::unique_ptr<ContainerManagerInterface> android_container,
    std::unique_ptr<ArcSideloadStatusInterface> arc_sideload_status)
    : system_utils_(system_utils),
      login_metrics_(login_metrics),
      bus_(bus),
      init_controller_(std::move(init_controller)),
      debugd_proxy_(debugd_proxy),
      android_container_(std::move(android_container)),
      arc_sideload_status_(std::move(arc_sideload_status)) {}

ArcManager::~ArcManager() = default;

std::unique_ptr<ArcManager> ArcManager::CreateForTesting(
    SystemUtils& system_utils,
    LoginMetrics& login_metrics,
    scoped_refptr<dbus::Bus> bus,
    std::unique_ptr<InitDaemonController> init_controller,
    dbus::ObjectProxy* debugd_proxy,
    std::unique_ptr<ContainerManagerInterface> android_container,
    std::unique_ptr<ArcSideloadStatusInterface> arc_sideload_status) {
  // std::make_unique won't work because the target ctor is private.
  return base::WrapUnique(new ArcManager(
      system_utils, login_metrics, bus, std::move(init_controller),
      debugd_proxy, std::move(android_container),
      std::move(arc_sideload_status)));
}

void ArcManager::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void ArcManager::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void ArcManager::Initialize() {
  arc_sideload_status_->Initialize();
}

void ArcManager::Finalize() {
  dbus_service_.reset();

  // We want to stop all running containers and VMs.  Containers and VMs are
  // per-session and cannot persist across sessions.
  android_container_->RequestJobExit(
      ArcContainerStopReason::SESSION_MANAGER_SHUTDOWN);
  android_container_->EnsureJobExit(kContainerTimeout);

  // TODO(hidehiko): consider to make this lifetime tied to Initialize/Finalize
  // as a pair of the methods. Or, move the destruction to the dtor.
  arc_sideload_status_.reset();
}

bool ArcManager::StartDBusService() {
  CHECK(!dbus_service_);
  auto dbus_service = std::make_unique<DBusService>(adaptor_);
  if (!dbus_service->Start(bus_)) {
    return false;
  }

  dbus_service_ = std::move(dbus_service);
  return true;
}

bool ArcManager::IsAdbSideloadAllowed() const {
  return arc_sideload_status_->IsAdbSideloadAllowed();
}

void ArcManager::OnUpgradeArcContainer() {
  // |arc_start_time_| is initialized when the container is upgraded (rather
  // than when the mini-container starts) since we are interested in measuring
  // time from when the user logs in until the system is ready to be interacted
  // with.
  arc_start_time_ = base::TimeTicks::Now();
}

void ArcManager::EmitStopArcVmInstanceImpulse() {
  if (!init_controller_->TriggerImpulse(
          kStopArcVmInstanceImpulse, {},
          InitDaemonController::TriggerMode::SYNC)) {
    LOG(ERROR) << "Emitting stop-arcvm-instance impulse failed.";
  }
}

void ArcManager::RequestJobExit(uint32_t reason) {
  android_container_->RequestJobExit(
      static_cast<ArcContainerStopReason>(reason));
}

void ArcManager::EnsureJobExit(int64_t timeout_ms) {
  android_container_->EnsureJobExit(base::Milliseconds(timeout_ms));
}

void ArcManager::OnUserSessionStarted(const std::string& in_account_id) {
  user_sessions_.insert(in_account_id);
  DeleteArcBugReportBackup(in_account_id);
}

bool ArcManager::StartArcMiniContainer(brillo::ErrorPtr* error,
                                       const std::vector<uint8_t>& in_request) {
#if USE_CHEETS
  arc::StartArcMiniInstanceRequest request;
  if (!request.ParseFromArray(in_request.data(), in_request.size())) {
    *error = CreateError(DBUS_ERROR_INVALID_ARGS,
                         "StartArcMiniInstanceRequest parsing failed.");
    return false;
  }

  std::vector<std::string> env_vars = {
      base::StringPrintf("CHROMEOS_DEV_MODE=%d", IsDevMode(*system_utils_)),
      base::StringPrintf("CHROMEOS_INSIDE_VM=%d", IsInsideVm(*system_utils_)),
      base::StringPrintf("NATIVE_BRIDGE_EXPERIMENT=%d",
                         request.native_bridge_experiment()),
      base::StringPrintf("DISABLE_MEDIA_STORE_MAINTENANCE=%d",
                         request.disable_media_store_maintenance()),
      base::StringPrintf("DISABLE_DOWNLOAD_PROVIDER=%d",
                         request.disable_download_provider()),
      base::StringPrintf("ENABLE_CONSUMER_AUTO_UPDATE_TOGGLE=%d",
                         request.enable_consumer_auto_update_toggle()),
      base::StringPrintf("ENABLE_PRIVACY_HUB_FOR_CHROME=%d",
                         request.enable_privacy_hub_for_chrome()),
      base::StringPrintf("ENABLE_TTS_CACHING=%d", request.enable_tts_caching()),
      base::StringPrintf("USE_DEV_CACHES=%d", request.use_dev_caches()),
      base::StringPrintf("ARC_SIGNED_IN=%d", request.arc_signed_in()),
  };

  if (request.arc_generate_pai()) {
    env_vars.push_back("ARC_GENERATE_PAI=1");
  }

  if (request.lcd_density() > 0) {
    env_vars.push_back(
        base::StringPrintf("ARC_LCD_DENSITY=%d", request.lcd_density()));
  }

  switch (request.play_store_auto_update()) {
    case arc::
        StartArcMiniInstanceRequest_PlayStoreAutoUpdate_AUTO_UPDATE_DEFAULT:
      break;
    case arc::StartArcMiniInstanceRequest_PlayStoreAutoUpdate_AUTO_UPDATE_ON:
      env_vars.emplace_back("PLAY_STORE_AUTO_UPDATE=1");
      break;
    case arc::StartArcMiniInstanceRequest_PlayStoreAutoUpdate_AUTO_UPDATE_OFF:
      env_vars.emplace_back("PLAY_STORE_AUTO_UPDATE=0");
      break;
    default:
      NOTREACHED_IN_MIGRATION() << "Unhandled play store auto-update mode: "
                                << request.play_store_auto_update() << ".";
  }

  switch (request.dalvik_memory_profile()) {
    case kStartArcMiniInstanceRequest_DalvikMemoryProfile_MEM_PROFILE_DEFAULT:
      break;
    case arc::StartArcMiniInstanceRequest_DalvikMemoryProfile_MEMORY_PROFILE_4G:
      env_vars.emplace_back("DALVIK_MEMORY_PROFILE=4G");
      break;
    case arc::StartArcMiniInstanceRequest_DalvikMemoryProfile_MEMORY_PROFILE_8G:
      env_vars.emplace_back("DALVIK_MEMORY_PROFILE=8G");
      break;
    case arc::
        StartArcMiniInstanceRequest_DalvikMemoryProfile_MEMORY_PROFILE_16G:
      env_vars.emplace_back("DALVIK_MEMORY_PROFILE=16G");
      break;
    default:
      NOTREACHED_IN_MIGRATION() << "Unhandled dalvik_memory_profle: "
                                << request.dalvik_memory_profile() << ".";
  }

  switch (request.host_ureadahead_mode()) {
    case kStartArcMiniInstanceRequest_HostUreadaheadMode_MODE_DEFAULT:
      env_vars.emplace_back("HOST_UREADAHEAD_MODE=DEFAULT");
      break;
    case arc::StartArcMiniInstanceRequest_HostUreadaheadMode_MODE_GENERATE:
      env_vars.emplace_back("HOST_UREADAHEAD_MODE=GENERATE");
      break;
    case arc::StartArcMiniInstanceRequest_HostUreadaheadMode_MODE_DISABLED:
      env_vars.emplace_back("HOST_UREADAHEAD_MODE=DISABLED");
      break;
    default:
      NOTREACHED_IN_MIGRATION() << "Unhandled host_ureadahead_mode: "
                                << request.host_ureadahead_mode() << ".";
  }

  if (!StartArcContainer(env_vars, error)) {
    DCHECK(*error);
    return false;
  }
  return true;
#else
  *error = CreateError(dbus_error::kNotAvailable, "ARC not supported.");
  return false;
#endif  // !USE_CHEETS
}

bool ArcManager::UpgradeArcContainer(brillo::ErrorPtr* error,
                                     const std::vector<uint8_t>& in_request) {
#if USE_CHEETS
  // Stop the existing instance if it fails to continue to boot an existing
  // container. Using Unretained() is okay because the closure will be called
  // before this function returns. If container was not running, this is no op.
  base::ScopedClosureRunner scoped_runner(base::BindOnce(
      &ArcManager::OnContinueArcBootFailed, base::Unretained(this)));

  arc::UpgradeArcContainerRequest request;
  if (!request.ParseFromArray(in_request.data(), in_request.size())) {
    *error = CreateError(DBUS_ERROR_INVALID_ARGS,
                         "UpgradeArcContainerRequest parsing failed.");
    return false;
  }

  pid_t pid = 0;
  if (!android_container_->GetContainerPID(&pid)) {
    *error = CREATE_ERROR_AND_LOG(dbus_error::kArcContainerNotFound,
                                  "Failed to find mini-container for upgrade.");
    return false;
  }
  LOG(INFO) << "Android container is running with PID " << pid;

  OnUpgradeArcContainer();

  // To upgrade the ARC mini-container, a certain amount of disk space is
  // needed under /home. We first check it.
  auto free_disk_space_optional =
      system_utils_->AmountOfFreeDiskSpace(base::FilePath(kArcDiskCheckPath));
  if (!free_disk_space_optional.has_value() ||
      free_disk_space_optional.value() < kArcCriticalDiskFreeBytes) {
    *error = CREATE_ERROR_AND_LOG(dbus_error::kLowFreeDisk,
                                  "Low free disk under /home");
    StopArcInstanceInternal(ArcContainerStopReason::LOW_DISK_SPACE);
    scoped_runner.ReplaceClosure(base::DoNothing());
    return false;
  }

  std::string account_id;
  if (!ValidateAccountId(request.account_id(), &account_id)) {
    // TODO(alemate): adjust this error message after ChromeOS will stop using
    // email as cryptohome identifier.
    *error = CREATE_ERROR_AND_LOG(
        dbus_error::kInvalidAccount,
        "Provided email address is not valid.  ASCII only.");
    return false;
  }
  if (user_sessions_.find(account_id) == user_sessions_.end()) {
    // This path can be taken if a forged D-Bus message for starting a full
    // (stateful) container is sent to session_manager before the actual
    // user's session has started. Do not remove the |account_id| check to
    // prevent such a container from starting on login screen.
    *error = CREATE_ERROR_AND_LOG(dbus_error::kSessionDoesNotExist,
                                  "Provided user ID does not have a session.");
    return false;
  }

  android_container_->SetStatefulMode(StatefulMode::STATEFUL);
  auto env_vars = CreateUpgradeArcEnvVars(request, account_id, pid);

  dbus::Error dbus_error;
  std::unique_ptr<dbus::Response> response =
      init_controller_->TriggerImpulseWithTimeoutAndError(
          kContinueArcBootImpulse, env_vars,
          InitDaemonController::TriggerMode::SYNC, kArcBootContinueTimeout,
          &dbus_error);
  LoginMetrics::ArcContinueBootImpulseStatus status =
      GetArcContinueBootImpulseStatus(&dbus_error);
  login_metrics_->SendArcContinueBootImpulseStatus(status);

  if (!response) {
    *error = CREATE_ERROR_AND_LOG(dbus_error::kEmitFailed,
                                  "Emitting continue-arc-boot impulse failed.");

    BackupArcBugReport(account_id);
    return false;
  }

  login_metrics_->StartTrackingArcUseTime();

  scoped_runner.ReplaceClosure(base::DoNothing());
  DeleteArcBugReportBackup(account_id);

  return true;
#else
  *error = CreateError(dbus_error::kNotAvailable, "ARC not supported.");
  return false;
#endif  // !USE_CHEETS
}

bool ArcManager::StopArcInstance(brillo::ErrorPtr* error,
                                 const std::string& account_id,
                                 bool should_backup_log) {
#if USE_CHEETS
  if (should_backup_log && !account_id.empty()) {
    std::string actual_account_id;
    if (!ValidateAccountId(account_id, &actual_account_id)) {
      // TODO(alemate): adjust this error message after ChromeOS will stop using
      // email as cryptohome identifier.
      *error = CREATE_ERROR_AND_LOG(
          dbus_error::kInvalidAccount,
          "Provided email address is not valid.  ASCII only.");
      return false;
    }

    BackupArcBugReport(actual_account_id);
  }

  if (!StopArcInstanceInternal(ArcContainerStopReason::USER_REQUEST)) {
    *error = CREATE_ERROR_AND_LOG(dbus_error::kContainerShutdownFail,
                                  "Error getting Android container pid.");
    return false;
  }

  return true;
#else
  *error = CreateError(dbus_error::kNotAvailable, "ARC not supported.");
  return false;
#endif  // USE_CHEETS
}

bool ArcManager::SetArcCpuRestriction(brillo::ErrorPtr* error,
                                      uint32_t in_restriction_state) {
#if USE_CHEETS
  std::string shares_out;
  switch (static_cast<ContainerCpuRestrictionState>(in_restriction_state)) {
    case CONTAINER_CPU_RESTRICTION_FOREGROUND:
      shares_out = std::to_string(kCpuSharesForeground);
      break;
    case CONTAINER_CPU_RESTRICTION_BACKGROUND:
      shares_out = std::to_string(kCpuSharesBackground);
      break;
    default:
      *error = CREATE_ERROR_AND_LOG(dbus_error::kArcCpuCgroupFail,
                                    "Invalid CPU restriction state specified.");
      return false;
  }
  if (!base::WriteFile(base::FilePath(kCpuSharesFile), shares_out)) {
    *error =
        CREATE_ERROR_AND_LOG(dbus_error::kArcCpuCgroupFail,
                             "Error updating Android container's cgroups.");
    return false;
  }
  return true;
#else
  *error = CreateError(dbus_error::kNotAvailable, "ARC not supported.");
  return false;
#endif
}

bool ArcManager::EmitArcBooted(brillo::ErrorPtr* error,
                               const std::string& in_account_id) {
#if USE_CHEETS
  std::vector<std::string> env_vars;
  if (!in_account_id.empty()) {
    std::string actual_account_id;
    if (!ValidateAccountId(in_account_id, &actual_account_id)) {
      // TODO(alemate): adjust this error message after ChromeOS will stop using
      // email as cryptohome identifier.
      *error = CREATE_ERROR_AND_LOG(
          dbus_error::kInvalidAccount,
          "Provided email address is not valid.  ASCII only.");
      return false;
    }
    env_vars.emplace_back("CHROMEOS_USER=" + actual_account_id);
  }

  init_controller_->TriggerImpulse(kArcBootedImpulse, env_vars,
                                   InitDaemonController::TriggerMode::ASYNC);
  return true;
#else
  *error = CreateError(dbus_error::kNotAvailable, "ARC not supported.");
  return false;
#endif
}

bool ArcManager::GetArcStartTimeTicks(brillo::ErrorPtr* error,
                                      int64_t* out_start_time) {
#if USE_CHEETS
  if (arc_start_time_.is_null()) {
    *error = CreateError(dbus_error::kNotStarted, "ARC is not started yet.");
    return false;
  }
  *out_start_time = arc_start_time_.ToInternalValue();
  return true;
#else
  *error = CreateError(dbus_error::kNotAvailable, "ARC not supported.");
  return false;
#endif  // !USE_CHEETS
}

void ArcManager::EnableAdbSideload(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  if (system_utils_->Exists(base::FilePath(kLoggedInFlag))) {
    auto error = CREATE_ERROR_AND_LOG(dbus_error::kSessionExists,
                                      "EnableAdbSideload is not allowed "
                                      "once a user logged in this boot.");
    response->ReplyWithError(error.get());
    return;
  }

  arc_sideload_status_->EnableAdbSideload(
      base::BindOnce(&ArcManager::EnableAdbSideloadCallbackAdaptor,
                     weak_factory_.GetWeakPtr(), std::move(response)));
}

void ArcManager::QueryAdbSideload(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  arc_sideload_status_->QueryAdbSideload(
      base::BindOnce(&ArcManager::QueryAdbSideloadCallbackAdaptor,
                     weak_factory_.GetWeakPtr(), std::move(response)));
}

#if USE_CHEETS
bool ArcManager::StartArcContainer(const std::vector<std::string>& env_vars,
                                   brillo::ErrorPtr* error_out) {
  init_controller_->TriggerImpulse(kStartArcInstanceImpulse, env_vars,
                                   InitDaemonController::TriggerMode::ASYNC);

  // Pass in the same environment variables that were passed to arc-setup
  // (through init, above) into the container invocation as environment values.
  // When the container is started with run_oci, this allows for it to correctly
  // propagate some information (such as the CHROMEOS_USER) to the hooks so it
  // can set itself up.
  if (!android_container_->StartContainer(
          env_vars, base::BindOnce(&ArcManager::OnAndroidContainerStopped,
                                   weak_factory_.GetWeakPtr()))) {
    // Failed to start container. Thus, trigger stop-arc-instance impulse
    // manually for cleanup.
    init_controller_->TriggerImpulse(kStopArcInstanceImpulse, {},
                                     InitDaemonController::TriggerMode::SYNC);
    *error_out = CREATE_ERROR_AND_LOG(dbus_error::kContainerStartupFail,
                                      "Starting Android container failed.");
    return false;
  }

  pid_t pid = 0;
  android_container_->GetContainerPID(&pid);
  LOG(INFO) << "Started Android container with PID " << pid;
  return true;
}

std::vector<std::string> ArcManager::CreateUpgradeArcEnvVars(
    const arc::UpgradeArcContainerRequest& request,
    const std::string& account_id,
    pid_t pid) {
  // Only allow for managed account if the policies allow it.
  bool is_adb_sideloading_allowed_for_request =
      !request.is_account_managed() ||
      request.is_managed_adb_sideloading_allowed();

  std::vector<std::string> env_vars = {
      base::StringPrintf("CHROMEOS_DEV_MODE=%d", IsDevMode(*system_utils_)),
      base::StringPrintf("CHROMEOS_INSIDE_VM=%d", IsInsideVm(*system_utils_)),
      "CHROMEOS_USER=" + account_id,
      base::StringPrintf("DISABLE_BOOT_COMPLETED_BROADCAST=%d",
                         request.skip_boot_completed_broadcast()),
      base::StringPrintf("CONTAINER_PID=%d", pid),
      "DEMO_SESSION_APPS_PATH=" + request.demo_session_apps_path(),
      base::StringPrintf("IS_DEMO_SESSION=%d", request.is_demo_session()),
      base::StringPrintf("MANAGEMENT_TRANSITION=%d",
                         request.management_transition()),
      base::StringPrintf(
          "ENABLE_ADB_SIDELOAD=%d",
          IsAdbSideloadAllowed() && is_adb_sideloading_allowed_for_request),
      base::StringPrintf("ENABLE_ARC_NEARBY_SHARE=%d",
                         request.enable_arc_nearby_share())};

  switch (request.packages_cache_mode()) {
    case arc::
        UpgradeArcContainerRequest_PackageCacheMode_SKIP_SETUP_COPY_ON_INIT:
      env_vars.emplace_back("SKIP_PACKAGES_CACHE_SETUP=1");
      env_vars.emplace_back("COPY_PACKAGES_CACHE=1");
      break;
    case arc::UpgradeArcContainerRequest_PackageCacheMode_COPY_ON_INIT:
      env_vars.emplace_back("SKIP_PACKAGES_CACHE_SETUP=0");
      env_vars.emplace_back("COPY_PACKAGES_CACHE=1");
      break;
    case arc::UpgradeArcContainerRequest_PackageCacheMode_DEFAULT:
      env_vars.emplace_back("SKIP_PACKAGES_CACHE_SETUP=0");
      env_vars.emplace_back("COPY_PACKAGES_CACHE=0");
      break;
    default:
      NOTREACHED_IN_MIGRATION()
          << "Wrong packages cache mode: " << request.packages_cache_mode()
          << ".";
  }

  if (request.skip_gms_core_cache()) {
    env_vars.emplace_back("SKIP_GMS_CORE_CACHE_SETUP=1");
  } else {
    env_vars.emplace_back("SKIP_GMS_CORE_CACHE_SETUP=0");
  }

  if (request.skip_tts_cache()) {
    env_vars.emplace_back("SKIP_TTS_CACHE_SETUP=1");
  } else {
    env_vars.emplace_back("SKIP_TTS_CACHE_SETUP=0");
  }

  DCHECK(request.has_locale());
  env_vars.emplace_back("LOCALE=" + request.locale());

  std::string preferred_languages;
  for (int i = 0; i < request.preferred_languages_size(); ++i) {
    if (i != 0) {
      preferred_languages += ",";
    }
    preferred_languages += request.preferred_languages(i);
  }
  env_vars.emplace_back("PREFERRED_LANGUAGES=" + preferred_languages);

  return env_vars;
}

void ArcManager::OnContinueArcBootFailed() {
  LOG(ERROR) << "Failed to continue ARC boot. Stopping the container.";
  StopArcInstanceInternal(ArcContainerStopReason::UPGRADE_FAILURE);
}

bool ArcManager::StopArcInstanceInternal(ArcContainerStopReason reason) {
  pid_t pid;
  if (!android_container_->GetContainerPID(&pid)) {
    return false;
  }

  android_container_->RequestJobExit(reason);
  android_container_->EnsureJobExit(kContainerTimeout);
  return true;
}

void ArcManager::OnAndroidContainerStopped(pid_t pid,
                                           ArcContainerStopReason reason) {
  if (reason == ArcContainerStopReason::CRASH) {
    LOG(ERROR) << "Android container with PID " << pid << " crashed";
  } else {
    LOG(INFO) << "Android container with PID " << pid << " stopped";
  }

  login_metrics_->StopTrackingArcUseTime();
  if (!init_controller_->TriggerImpulse(
          kStopArcInstanceImpulse, {},
          InitDaemonController::TriggerMode::SYNC)) {
    LOG(ERROR) << "Emitting stop-arc-instance impulse failed.";
  }

  adaptor_.SendArcInstanceStoppedSignal(static_cast<uint32_t>(reason));
  for (auto& observer : observers_) {
    observer.OnArcInstanceStopped(static_cast<uint32_t>(reason));
  }
}

LoginMetrics::ArcContinueBootImpulseStatus
ArcManager::GetArcContinueBootImpulseStatus(dbus::Error* dbus_error) {
  DCHECK(dbus_error);
  if (dbus_error->IsValid()) {
    // In case of timeout we see DBUS_ERROR_NO_REPLY
    // as mentioned in dbus-protocol.h
    if (dbus_error->name() == DBUS_ERROR_NO_REPLY) {
      return LoginMetrics::ArcContinueBootImpulseStatus::
          kArcContinueBootImpulseStatusTimedOut;
    }
    return LoginMetrics::ArcContinueBootImpulseStatus::
        kArcContinueBootImpulseStatusFailed;
  }
  return LoginMetrics::ArcContinueBootImpulseStatus::
      kArcContinueBootImpulseStatusSuccess;
}
#endif  // USE_CHEETS

void ArcManager::EnableAdbSideloadCallbackAdaptor(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
    ArcSideloadStatusInterface::Status status,
    const char* error) {
  if (error != nullptr) {
    brillo::ErrorPtr dbus_error = CreateError(DBUS_ERROR_FAILED, error);
    response->ReplyWithError(dbus_error.get());
    return;
  }

  if (status == ArcSideloadStatusInterface::Status::NEED_POWERWASH) {
    brillo::ErrorPtr dbus_error = CreateError(DBUS_ERROR_NOT_SUPPORTED, error);
    response->ReplyWithError(dbus_error.get());
    return;
  }

  response->Return(status == ArcSideloadStatusInterface::Status::ENABLED);
}

void ArcManager::QueryAdbSideloadCallbackAdaptor(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
    ArcSideloadStatusInterface::Status status) {
  if (status == ArcSideloadStatusInterface::Status::NEED_POWERWASH) {
    brillo::ErrorPtr dbus_error =
        CreateError(DBUS_ERROR_NOT_SUPPORTED, "Need powerwash");
    response->ReplyWithError(dbus_error.get());
    return;
  }

  response->Return(status == ArcSideloadStatusInterface::Status::ENABLED);
}

void ArcManager::BackupArcBugReport(const std::string& account_id) {
  if (user_sessions_.find(account_id) == user_sessions_.end()) {
    LOG(ERROR) << "Cannot back up ARC bug report for inactive user.";
    return;
  }

  dbus::MethodCall method_call(debugd::kDebugdInterface,
                               debugd::kBackupArcBugReport);
  dbus::MessageWriter writer(&method_call);

  writer.AppendString(account_id);

  base::expected<std::unique_ptr<dbus::Response>, dbus::Error> response(
      debugd_proxy_->CallMethodAndBlock(
          &method_call, kBackupArcBugReportTimeout.InMilliseconds()));

  if (!response.has_value() || !response.value()) {
    LOG(ERROR) << "Error contacting debugd to back up ARC bug report.";
  }
}

void ArcManager::DeleteArcBugReportBackup(const std::string& account_id) {
  dbus::MethodCall method_call(debugd::kDebugdInterface,
                               debugd::kDeleteArcBugReportBackup);
  dbus::MessageWriter writer(&method_call);

  writer.AppendString(account_id);

  base::expected<std::unique_ptr<dbus::Response>, dbus::Error> response(
      debugd_proxy_->CallMethodAndBlock(
          &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT));

  if (!response.has_value() || !response.value()) {
    LOG(ERROR) << "Error contacting debugd to delete ARC bug report backup.";
  }
}

}  // namespace login_manager
