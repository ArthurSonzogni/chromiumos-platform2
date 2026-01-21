// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/session_manager_impl.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>

#include <algorithm>
#include <locale>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include <base/base64.h>
#include <base/check.h>
#include <base/containers/fixed_flat_map.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/rand_util.h>
#include <base/run_loop.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_tokenizer.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/system/sys_info.h>
#include <base/task/single_thread_task_runner.h>
#include <base/time/default_tick_clock.h>
#include <base/time/time.h>
#include <base/types/expected.h>
#include <brillo/cryptohome.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/dbus/utils.h>
#include <brillo/files/file_util.h>
#include <brillo/scoped_mount_namespace.h>
#include <chromeos/dbus/service_constants.h>
#include <crypto/scoped_nss_types.h>
#include <dbus/error.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>
#include <install_attributes/libinstallattributes.h>
#include <libcrossystem/crossystem.h>
#include <libpasswordprovider/password.h>
#include <libpasswordprovider/password_provider.h>
#include <vpd/vpd.h>

#include "bindings/chrome_device_policy.pb.h"
#include "bindings/device_management_backend.pb.h"
#include "dbus/login_manager/dbus-constants.h"
#include "login_manager/blob_util.h"
#include "login_manager/browser_job.h"
#include "login_manager/dbus_util.h"
#include "login_manager/device_local_account_manager.h"
#include "login_manager/device_policy_service.h"
#include "login_manager/init_daemon_controller.h"
#include "login_manager/nss_util.h"
#include "login_manager/policy_key.h"
#include "login_manager/policy_service.h"
#include "login_manager/policy_store.h"
#include "login_manager/process_manager_service_interface.h"
#include "login_manager/proto_bindings/login_screen_storage.pb.h"
#include "login_manager/proto_bindings/policy_descriptor.pb.h"
#include "login_manager/secret_util.h"
#include "login_manager/system_utils.h"
#include "login_manager/user_policy_service_factory.h"
#include "login_manager/validator_utils.h"
#include "login_manager/vpd_process.h"

using base::FilePath;
using brillo::cryptohome::home::GetGuestUsername;
using brillo::cryptohome::home::GetUserPath;
using brillo::cryptohome::home::SanitizeUserName;
using brillo::cryptohome::home::Username;

namespace login_manager {  // NOLINT

constexpr char SessionManagerImpl::kStarted[] = "started";
constexpr char SessionManagerImpl::kStopping[] = "stopping";
constexpr char SessionManagerImpl::kStopped[] = "stopped";

constexpr char SessionManagerImpl::kLoggedInFlag[] =
    "/run/session_manager/logged_in";
constexpr char SessionManagerImpl::kResetFile[] =
    "/mnt/stateful_partition/factory_install_reset";

constexpr char SessionManagerImpl::kTPMFirmwareUpdateLocationFile[] =
    "/run/tpm_firmware_update_location";
constexpr char SessionManagerImpl::kTPMFirmwareUpdateSRKVulnerableROCAFile[] =
    "/run/tpm_firmware_update_srk_vulnerable_roca";
constexpr char SessionManagerImpl::kTPMFirmwareUpdateRequestFlagFile[] =
    "/mnt/stateful_partition/unencrypted/preserve/tpm_firmware_update_request";
constexpr char SessionManagerImpl::kStatefulPreservationRequestFile[] =
    "/mnt/stateful_partition/preservation_request";

constexpr char SessionManagerImpl::kStartUserSessionImpulse[] =
    "start-user-session";
constexpr char SessionManagerImpl::kStartedUserSessionImpulse[] =
    "started-user-session";

constexpr char SessionManagerImpl::kLoadShillProfileImpulse[] =
    "load-shill-profile";

// Lock state related impulse (systemd unit start or Upstart signal).
constexpr char SessionManagerImpl::kScreenLockedImpulse[] = "screen-locked";
constexpr char SessionManagerImpl::kScreenUnlockedImpulse[] = "screen-unlocked";

constexpr base::TimeDelta SessionManagerImpl::kKeyGenTimeout = base::Seconds(1);

constexpr base::TimeDelta SessionManagerImpl::kCrashBeforeSuspendInterval =
    base::Seconds(5);
constexpr base::TimeDelta SessionManagerImpl::kCrashAfterSuspendInterval =
    base::Seconds(5);

namespace {

// The flag to pass to chrome to open a named socket for testing.
const char kTestingChannelFlag[] = "--testing-channel=NamedTestingInterface:";

// The interval used to periodically check if time sync was done by tlsdated.
constexpr base::TimeDelta kSystemClockLastSyncInfoRetryDelay =
    base::Milliseconds(1000);

// TPM firmware update modes.
constexpr char kTPMFirmwareUpdateModeFirstBoot[] = "first_boot";
constexpr char kTPMFirmwareUpdateModePreserveStateful[] = "preserve_stateful";
constexpr char kTPMFirmwareUpdateModeCleanup[] = "cleanup";

// Policy storage constants.
constexpr char kSigEncodeFailMessage[] = "Failed to retrieve policy data.";
constexpr char kParseDescriptorFailMessage[] =
    "Failed to parse policy descriptor.";
constexpr char kGetPolicyServiceFailMessage[] = "Failed to get policy service.";

// Default path of symlink to log file where stdout and stderr from
// session_manager and Chrome are redirected.
constexpr char kDefaultUiLogSymlinkPath[] = "/var/log/ui/ui.LATEST";

constexpr auto kStateKeysComputationErrorMessages = base::MakeFixedFlatMap<
    DeviceIdentifierGenerator::StateKeysComputationError,
    std::string_view>({
    {DeviceIdentifierGenerator::StateKeysComputationError::
         kMalformedDeviceSecret,
     "Malformed device secret"},
    {DeviceIdentifierGenerator::StateKeysComputationError::
         kHmacInitializationError,
     "Failed to init HMAC"},
    {DeviceIdentifierGenerator::StateKeysComputationError::
         kHmacComputationError,
     "Failed to compute HMAC"},
    {DeviceIdentifierGenerator::StateKeysComputationError::
         kMissingAllDeviceIdentifiers,
     "Missing all device identifiers"},
    {DeviceIdentifierGenerator::StateKeysComputationError::kMissingSerialNumber,
     "Missing serial number"},
    {DeviceIdentifierGenerator::StateKeysComputationError::
         kMissingDiskSerialNumber,
     "Missing disk serial number"},
    {DeviceIdentifierGenerator::StateKeysComputationError::
         kMalformedReEnrollmentKey,
     "Malformed re-enrollment key"},
});

const char* ToSuccessSignal(bool success) {
  return success ? "success" : "failure";
}

// Parses |descriptor_blob| into |descriptor| and validates it assuming the
// given |usage|. Returns true and sets |descriptor| on success. Returns false
// and sets |error| on failure.
bool ParseAndValidatePolicyDescriptor(
    const std::vector<uint8_t>& descriptor_blob,
    PolicyDescriptorUsage usage,
    PolicyDescriptor* descriptor,
    brillo::ErrorPtr* error) {
  DCHECK(descriptor);
  DCHECK(error);
  if (!descriptor->ParseFromArray(descriptor_blob.data(),
                                  descriptor_blob.size())) {
    *error = CreateError(DBUS_ERROR_INVALID_ARGS,
                         "PolicyDescriptor parsing failed.");
    return false;
  }

  if (!ValidatePolicyDescriptor(*descriptor, usage)) {
    *error = CreateError(DBUS_ERROR_INVALID_ARGS, "PolicyDescriptor invalid.");
    return false;
  }

  return true;
}

// Handles the result of an attempt to connect to a D-Bus signal, logging an
// error on failure.
void HandleDBusSignalConnected(const std::string& interface,
                               const std::string& signal,
                               bool success) {
  if (!success) {
    LOG(ERROR) << "Failed to connect to D-Bus signal " << interface << "."
               << signal;
  }
}

// Replaces the log file that |symlink_path| (typically /var/log/ui/ui.LATEST)
// points to with a new file containing the same contents. This is used to
// disconnect Chrome's stderr and stdout after a user logs in:
// https://crbug.com/904850.
void DisconnectLogFile(const base::FilePath& symlink_path) {
  base::FilePath log_path;
  if (!base::ReadSymbolicLink(symlink_path, &log_path)) {
    return;
  }

  if (!log_path.IsAbsolute()) {
    log_path = symlink_path.DirName().Append(log_path);
  }

  // Perform a basic safety check.
  if (log_path.DirName() != symlink_path.DirName()) {
    LOG(WARNING) << "Log file " << log_path.value() << " isn't in same "
                 << "directory as symlink " << symlink_path.value()
                 << "; not disconnecting it";
    return;
  }

  // Copy the contents to a temp file and then move it over the original path.
  base::FilePath temp_path;
  if (!base::CreateTemporaryFileInDir(log_path.DirName(), &temp_path)) {
    PLOG(WARNING) << "Failed to create temp file in "
                  << log_path.DirName().value();
    return;
  }
  if (!base::CopyFile(log_path, temp_path)) {
    PLOG(WARNING) << "Failed to copy " << log_path.value() << " to "
                  << temp_path.value();
    return;
  }

  // Try to to copy permissions so the new file isn't 0600, which makes it hard
  // to investigate issues on non-dev devices.
  int mode = 0;
  if (!base::GetPosixFilePermissions(log_path, &mode) ||
      !base::SetPosixFilePermissions(temp_path, mode)) {
    PLOG(WARNING) << "Failed to copy permissions from " << log_path.value()
                  << " to " << temp_path.value();
  }

  if (!base::ReplaceFile(temp_path, log_path, nullptr /* error */)) {
    PLOG(WARNING) << "Failed to rename " << temp_path.value() << " to "
                  << log_path.value();
  }
}

bool IsGuestMode(uint32_t mode) {
  return static_cast<SessionManagerImpl::RestartJobMode>(mode) ==
         SessionManagerImpl::RestartJobMode::kGuest;
}

bool IsGuestSession(const std::vector<std::string>& argv) {
  return std::ranges::contains(argv, BrowserJobInterface::kGuestSessionFlag);
}

}  // namespace

// Tracks D-Bus service running.
// Create*Callback functions return a callback adaptor from given
// DBusMethodResponse. These cancel in-progress operations when the instance is
// deleted.
class SessionManagerImpl::DBusService {
 public:
  explicit DBusService(org::chromium::SessionManagerInterfaceAdaptor* adaptor)
      : adaptor_(adaptor), weak_ptr_factory_(this) {}
  DBusService(const DBusService&) = delete;
  DBusService& operator=(const DBusService&) = delete;

  ~DBusService() = default;

  bool Start(const scoped_refptr<dbus::Bus>& bus) {
    DCHECK(!dbus_object_);

    // Registers the SessionManagerInterface D-Bus methods and signals.
    dbus_object_ = std::make_unique<brillo::dbus_utils::DBusObject>(
        nullptr, bus,
        org::chromium::SessionManagerInterfaceAdaptor::GetObjectPath());
    adaptor_->RegisterWithDBusObject(dbus_object_.get());
    dbus_object_->RegisterAndBlock();

    // Note that this needs to happen *after* all methods are exported
    // (http://crbug.com/331431).
    // This should pass dbus::Bus::REQUIRE_PRIMARY once on the new libchrome.
    return bus->RequestOwnershipAndBlock(kSessionManagerServiceName,
                                         dbus::Bus::REQUIRE_PRIMARY);
  }

  // Adaptor from DBusMethodResponse to PolicyService::Completion callback.
  PolicyService::Completion CreatePolicyServiceCompletionCallback(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response) {
    return base::BindOnce(&DBusService::HandlePolicyServiceCompletion,
                          weak_ptr_factory_.GetWeakPtr(), std::move(response));
  }

  // Adaptor from DBusMethodResponse to
  // DeviceIdentifierGenerator::StateKeyCallback callback.
  DeviceIdentifierGenerator::StateKeyCallback CreateStateKeyCallback(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          std::vector<std::vector<uint8_t>>>> response) {
    return base::BindOnce(&DBusService::HandleStateKeyCallback,
                          weak_ptr_factory_.GetWeakPtr(), std::move(response));
  }

  // Adaptor for DBusMethodResponse to
  // DeviceIdentifierGenerator::PsmDeviceActiveSecretCallback callback.
  DeviceIdentifierGenerator::PsmDeviceActiveSecretCallback
  CreatePsmDeviceActiveSecretCallback(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::string>>
          response) {
    return base::BindOnce(&DBusService::HandlePsmDeviceActiveSecretCallback,
                          weak_ptr_factory_.GetWeakPtr(), std::move(response));
  }

 private:
  void HandlePolicyServiceCompletion(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response,
      brillo::ErrorPtr error) {
    if (error) {
      response->ReplyWithError(error.get());
      return;
    }

    response->Return();
  }

  void HandleStateKeyCallback(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          std::vector<std::vector<uint8_t>>>> response,
      const base::expected<
          DeviceIdentifierGenerator::StateKeysList,
          DeviceIdentifierGenerator::StateKeysComputationError>& state_keys) {
    if (!state_keys.has_value()) {
      const auto error_message_it =
          kStateKeysComputationErrorMessages.find(state_keys.error());
      const brillo::ErrorPtr error = CreateError(
          dbus_error::kStateKeysRequestFail,
          error_message_it != kStateKeysComputationErrorMessages.end()
              ? std::string(error_message_it->second)
              : "Unknown error");
      return response->ReplyWithError(error.get());
    }

    return response->Return(state_keys.value());
  }

  void HandlePsmDeviceActiveSecretCallback(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::string>>
          response,
      const std::string& derived_secret) {
    response->Return(std::move(derived_secret));
  }

  org::chromium::SessionManagerInterfaceAdaptor* const adaptor_;
  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;

  base::WeakPtrFactory<DBusService> weak_ptr_factory_;
};

struct SessionManagerImpl::UserSession {
 public:
  UserSession(const std::string& username,
              const std::string& userhash,
              bool is_incognito,
              std::unique_ptr<PolicyService> policy_service)
      : username(username),
        userhash(userhash),
        is_incognito(is_incognito),
        policy_service(std::move(policy_service)) {}
  ~UserSession() {}

  const std::string username;
  const std::string userhash;
  const bool is_incognito;
  std::unique_ptr<PolicyService> policy_service;
};

SessionManagerImpl::SessionManagerImpl(
    Delegate* delegate,
    std::unique_ptr<InitDaemonController> init_controller,
    const scoped_refptr<dbus::Bus>& bus,
    DeviceIdentifierGenerator* device_identifier_generator,
    ProcessManagerServiceInterface* manager,
    LoginMetrics* metrics,
    NssUtil* nss,
    std::optional<base::FilePath> ns_path,
    SystemUtils* system_utils,
    crossystem::Crossystem* crossystem,
    VpdProcess* vpd_process,
    PolicyKey* owner_key,
    ArcManagerProxy* arc_manager,
    InstallAttributesReader* install_attributes_reader,
    dbus::ObjectProxy* powerd_proxy,
    dbus::ObjectProxy* system_clock_proxy)
    : init_controller_(std::move(init_controller)),
      system_clock_last_sync_info_retry_delay_(
          kSystemClockLastSyncInfoRetryDelay),
      tick_clock_(std::make_unique<base::DefaultTickClock>()),
      bus_(bus),
      adaptor_(this),
      delegate_(delegate),
      device_identifier_generator_(device_identifier_generator),
      manager_(manager),
      login_metrics_(metrics),
      nss_(nss),
      chrome_mount_ns_path_(ns_path),
      system_utils_(system_utils),
      crossystem_(crossystem),
      vpd_process_(vpd_process),
      owner_key_(owner_key),
      install_attributes_reader_(install_attributes_reader),
      powerd_proxy_(powerd_proxy),
      system_clock_proxy_(system_clock_proxy),
      arc_manager_(arc_manager),
      ui_log_symlink_path_(kDefaultUiLogSymlinkPath),
      password_provider_(
          std::make_unique<password_provider::PasswordProvider>()),
      login_screen_storage_(std::make_unique<LoginScreenStorage>(
          system_utils_,
          base::FilePath(kLoginScreenStoragePath),
          std::make_unique<secret_util::SharedMemoryUtil>())),
      weak_ptr_factory_(this) {
  DCHECK(delegate_);
  if (arc_manager_) {
    arc_observation_.Observe(arc_manager_.get());
  }
}

SessionManagerImpl::~SessionManagerImpl() {
  device_policy_->set_delegate(nullptr);  // Could use WeakPtr instead?
}

void SessionManagerImpl::SetPolicyServicesForTesting(
    std::unique_ptr<DevicePolicyService> device_policy,
    std::unique_ptr<UserPolicyServiceFactory> user_policy_factory,
    std::unique_ptr<DeviceLocalAccountManager> device_local_account_manager) {
  device_policy_ = std::move(device_policy);
  user_policy_factory_ = std::move(user_policy_factory);
  device_local_account_manager_ = std::move(device_local_account_manager);
}

void SessionManagerImpl::SetTickClockForTesting(
    std::unique_ptr<base::TickClock> clock) {
  tick_clock_ = std::move(clock);
}

void SessionManagerImpl::SetUiLogSymlinkPathForTesting(
    const base::FilePath& path) {
  ui_log_symlink_path_ = path;
}

void SessionManagerImpl::SetLoginScreenStorageForTesting(
    std::unique_ptr<LoginScreenStorage> login_screen_storage) {
  login_screen_storage_ = std::move(login_screen_storage);
}

void SessionManagerImpl::AnnounceSessionStoppingIfNeeded() {
  if (session_started_) {
    session_stopping_ = true;
    DLOG(INFO) << "Emitting D-Bus signal SessionStateChanged: " << kStopping;
    adaptor_.SendSessionStateChangedSignal(kStopping);
  }
}

void SessionManagerImpl::AnnounceSessionStopped() {
  session_stopping_ = session_started_ = false;
  DLOG(INFO) << "Emitting D-Bus signal SessionStateChanged: " << kStopped;
  adaptor_.SendSessionStateChangedSignal(kStopped);
}

bool SessionManagerImpl::ShouldEndSession(std::string* reason_out) {
  auto set_reason = [&](const std::string& reason) {
    if (reason_out) {
      *reason_out = reason;
    }
  };

  if (screen_locked_) {
    set_reason("screen is locked");
    return true;
  }

  if (suspend_ongoing_) {
    set_reason("suspend ongoing");
    return true;
  }

  if (!last_suspend_done_time_.is_null()) {
    const base::TimeDelta time_since_suspend =
        tick_clock_->NowTicks() - last_suspend_done_time_;
    if (time_since_suspend <= kCrashAfterSuspendInterval) {
      set_reason("suspend completed recently");
      return true;
    }
  }

  set_reason("");
  return false;
}

std::vector<std::string> SessionManagerImpl::GetExtraCommandLineArguments() {
  return device_policy_->GetExtraCommandLineArguments();
}

bool SessionManagerImpl::Initialize() {
  powerd_proxy_->ConnectToSignal(
      power_manager::kPowerManagerInterface,
      power_manager::kSuspendImminentSignal,
      base::BindRepeating(&SessionManagerImpl::OnSuspendImminent,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce(&HandleDBusSignalConnected));
  powerd_proxy_->ConnectToSignal(
      power_manager::kPowerManagerInterface, power_manager::kSuspendDoneSignal,
      base::BindRepeating(&SessionManagerImpl::OnSuspendDone,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce(&HandleDBusSignalConnected));

  system_clock_proxy_->WaitForServiceToBeAvailable(
      base::BindOnce(&SessionManagerImpl::OnSystemClockServiceAvailable,
                     weak_ptr_factory_.GetWeakPtr()));

  // AD management (Chromad) is no longer supported, so devices in this mode
  // should fail to boot. Therefore, we request a device reboot, then
  // intentionally crash the chromeos-login service. By failing to boot the new
  // OS version, we force the automatic update to fail, making the device stay
  // in the previous version. Reference:
  // https://www.chromium.org/chromium-os/chromiumos-design-docs/boot-design/#rollback-protection-after-update
  // Note: We don't want to return `false` in this `Initialize()` method,
  // because that would trigger a device wipe.
  //
  // TODO(b/263367348): Fully remove the "enterprise_ad" device mode from
  // install attributes, when all supported devices are guaranteed to not have
  // this mode.
  if (install_attributes_reader_->GetAttribute(
          InstallAttributesReader::kAttrMode) ==
      InstallAttributesReader::kDeviceModeEnterpriseAD) {
    RestartDevice(
        "Device is in an unsupported management mode (Active Directory)");
    NOTREACHED() << "Device is in an unsupported management mode "
                    "(Active Directory) - crashing this service to "
                    "force ChromeOS boot to fail.";
  }

  // Note: If SetPolicyServicesForTesting has been called, all services have
  // already been set and initialized.
  if (!device_policy_) {
    device_policy_ = DevicePolicyService::Create(
        owner_key_, login_metrics_, nss_, system_utils_, crossystem_,
        vpd_process_, install_attributes_reader_);
    // Thinking about combining set_delegate() with the 'else' block below and
    // moving it down? Note that device_policy_->Initialize() might call
    // OnKeyPersisted() on the delegate, so be sure it's safe.
    device_policy_->set_delegate(this);
    if (!device_policy_->Initialize()) {
      return false;
    }

    DCHECK(!user_policy_factory_);
    user_policy_factory_ =
        std::make_unique<UserPolicyServiceFactory>(nss_, system_utils_);

    device_local_account_manager_ = std::make_unique<DeviceLocalAccountManager>(
        system_utils_, base::FilePath(kDeviceLocalAccountsDir), owner_key_),
    device_local_account_manager_->UpdateDeviceSettings(
        device_policy_->GetSettings());
    if (device_policy_->MayUpdateSystemSettings()) {
      device_policy_->UpdateSystemSettings(PolicyService::Completion());
    }
  } else {
    device_policy_->set_delegate(this);
  }

  return true;
}

void SessionManagerImpl::Finalize() {
  // Reset the SessionManagerDBusAdaptor first to ensure that it'll permit
  // any outstanding DBusMethodCompletion objects to be abandoned without
  // having been run (http://crbug.com/638774, http://crbug.com/725734).
  dbus_service_.reset();
}

bool SessionManagerImpl::StartDBusService() {
  DCHECK(!dbus_service_);
  auto dbus_service = std::make_unique<DBusService>(&adaptor_);
  if (!dbus_service->Start(bus_)) {
    return false;
  }

  dbus_service_ = std::move(dbus_service);
  return true;
}

void SessionManagerImpl::EmitLoginPromptVisible() {
  login_metrics_->RecordStats("login-prompt-visible");
  adaptor_.SendLoginPromptVisibleSignal();
  init_controller_->TriggerImpulse("login-prompt-visible", {},
                                   InitDaemonController::TriggerMode::ASYNC);
}

void SessionManagerImpl::EmitAshInitialized() {
  init_controller_->TriggerImpulse("ash-initialized", {},
                                   InitDaemonController::TriggerMode::ASYNC);
}

bool SessionManagerImpl::EnableChromeTesting(
    brillo::ErrorPtr* error,
    bool in_force_relaunch,
    const std::vector<std::string>& in_test_arguments,
    const std::vector<std::string>& in_test_environment_variables,
    std::string* out_filepath) {
  // Check to see if we already have Chrome testing enabled.
  bool already_enabled = !chrome_testing_path_.empty();

  if (!already_enabled) {
    base::FilePath temp_file_path;  // So we don't clobber chrome_testing_path_;
    if (!system_utils_->GetUniqueFilenameInWriteOnlyTempDir(&temp_file_path)) {
      *error = CreateError(dbus_error::kTestingChannelError,
                           "Could not create testing channel filename.");
      return false;
    }
    chrome_testing_path_ = temp_file_path;
  }

  if (!already_enabled || in_force_relaunch) {
    // Delete testing channel file if it already exists.
    system_utils_->RemoveFile(chrome_testing_path_);

    // Add testing channel argument to arguments.
    std::string testing_argument = kTestingChannelFlag;
    testing_argument.append(chrome_testing_path_.value());
    std::vector<std::string> test_args = in_test_arguments;
    test_args.push_back(testing_argument);
    manager_->SetBrowserTestArgs(test_args);
    manager_->SetBrowserAdditionalEnvironmentalVariables(
        in_test_environment_variables);
    manager_->RestartBrowser();
  }
  *out_filepath = chrome_testing_path_.value();
  return true;
}

bool SessionManagerImpl::LoginScreenStorageListKeys(
    brillo::ErrorPtr* error, std::vector<std::string>* out_keys) {
  *out_keys = login_screen_storage_->ListKeys();
  return true;
}

void SessionManagerImpl::LoginScreenStorageDelete(const std::string& in_key) {
  login_screen_storage_->Delete(in_key);
}

bool SessionManagerImpl::StartSession(brillo::ErrorPtr* error,
                                      const std::string& in_account_id,
                                      const std::string& in_unique_identifier) {
  return StartSessionEx(error, in_account_id, in_unique_identifier,
                        /*chrome_owner_key=*/true);
}

// TODO(b/259362896): StartSessionEx() and  `chrome_owner_key` were introduced
// as a part of the ChromeSideOwnerKeyGeneration experiment in Chrome. It is now
// always enabled and should be removed.
bool SessionManagerImpl::StartSessionEx(brillo::ErrorPtr* error,
                                        const std::string& in_account_id,
                                        const std::string& in_unique_identifier,
                                        bool /*chrome_owner_key*/) {
  std::string actual_account_id;
  if (!NormalizeAccountId(in_account_id, &actual_account_id, error)) {
    DCHECK(*error);
    return false;
  }

  // Check if this user already started a session.
  if (user_sessions_.count(actual_account_id) > 0) {
    *error =
        CREATE_ERROR_AND_LOG(dbus_error::kSessionExists,
                             "Provided user id already started a session.");
    return false;
  }

  const bool is_incognito = IsIncognitoAccountId(actual_account_id);

  auto user_session = CreateUserSession(actual_account_id, is_incognito, error);
  if (!user_session) {
    DCHECK(*error);
    return false;
  }

  // If all previous sessions were incognito (or no previous sessions exist).
  bool is_first_real_user = AllSessionsAreIncognito() && !is_incognito;

  // Make sure that Chrome's stdout and stderr, which may contain log messages
  // with user-specific data, don't get saved after the first user logs in:
  // https://crbug.com/904850.
  //
  // On test images, disable this behavior, so that developers can see
  // in-process crash dump which is printed to stderr (b/188858313). NOTE: Here
  // we check the image type instead of the device's mode, so that developers
  // can verify what's happening on user devices with a developer mode device
  // running a regular image.
  std::string channel_string;
  const bool is_test_image = base::SysInfo::GetLsbReleaseValue(
                                 "CHROMEOS_RELEASE_TRACK", &channel_string) &&
                             base::StartsWith(channel_string, "test");
  if (user_sessions_.empty() && !is_test_image) {
    DisconnectLogFile(ui_log_symlink_path_);
  }

  init_controller_->TriggerImpulse(kStartUserSessionImpulse,
                                   {"CHROMEOS_USER=" + actual_account_id},
                                   InitDaemonController::TriggerMode::ASYNC);
  LOG(INFO) << "Starting user session";
  manager_->SetBrowserSessionForUser(actual_account_id, user_session->userhash);
  session_started_ = true;
  user_sessions_[actual_account_id] = std::move(user_session);
  if (arc_manager_) {
    // In tests, arc_manager_ is nullptr.
    arc_manager_->OnUserSessionStarted(actual_account_id);
  }
  if (is_first_real_user) {
    DCHECK(primary_user_account_id_.empty());
    primary_user_account_id_ = actual_account_id;
  }
  DLOG(INFO) << "Emitting D-Bus signal SessionStateChanged: " << kStarted;
  adaptor_.SendSessionStateChangedSignal(kStarted);

  // Record that a login has successfully completed on this boot.
  system_utils_->WriteFileAtomically(base::FilePath(kLoggedInFlag),
                                     base::byte_span_from_cstring("1"),
                                     S_IRUSR | S_IWUSR | S_IROTH);
  return true;
}

bool SessionManagerImpl::EmitStartedUserSession(
    brillo::ErrorPtr* error, const std::string& in_account_id) {
  std::string actual_account_id;
  if (!NormalizeAccountId(in_account_id, &actual_account_id, error)) {
    DCHECK(*error);
    return false;
  }

  // Check if this user is starting a session.
  if (user_sessions_.count(actual_account_id) == 0) {
    *error = CREATE_ERROR_AND_LOG(dbus_error::kSessionNotExists,
                                  "Provided user id didn't start a session.");
    return false;
  }

  // Avoid re-emitting the signal for the same session.
  if (emitted_started_user_session_.find(actual_account_id) !=
      emitted_started_user_session_.end()) {
    return true;
  }

  init_controller_->TriggerImpulse(kStartedUserSessionImpulse,
                                   {"CHROMEOS_USER=" + actual_account_id},
                                   InitDaemonController::TriggerMode::ASYNC);
  emitted_started_user_session_.insert(actual_account_id);

  return true;
}

bool SessionManagerImpl::SaveLoginPassword(
    brillo::ErrorPtr* error, const base::ScopedFD& in_password_fd) {
  if (!secret_util::SaveSecretFromPipe(password_provider_.get(),
                                       in_password_fd)) {
    LOG(ERROR) << "Could not save password.";
    return false;
  }
  return true;
}

bool SessionManagerImpl::LoginScreenStorageStore(
    brillo::ErrorPtr* error,
    const std::string& in_key,
    const std::vector<uint8_t>& in_metadata,
    uint64_t in_value_size,
    const base::ScopedFD& in_value_fd) {
  LoginScreenStorageMetadata metadata;
  if (!metadata.ParseFromArray(in_metadata.data(), in_metadata.size())) {
    *error = CreateError(DBUS_ERROR_INVALID_ARGS, "metadata parsing failed.");
    return false;
  }

  if (!metadata.clear_on_session_exit() && !user_sessions_.empty()) {
    *error = CreateError(DBUS_ERROR_FAILED,
                         "can't store persistent login screen data while there "
                         "are active user sessions.");
    return false;
  }

  return login_screen_storage_->Store(error, in_key, metadata, in_value_size,
                                      in_value_fd);
}

bool SessionManagerImpl::LoginScreenStorageRetrieve(
    brillo::ErrorPtr* error,
    const std::string& in_key,
    uint64_t* out_value_size,
    base::ScopedFD* out_value_fd) {
  base::ScopedFD value_fd;
  bool success =
      login_screen_storage_->Retrieve(error, in_key, out_value_size, &value_fd);
  *out_value_fd = std::move(value_fd);
  return success;
}

void SessionManagerImpl::StopSession(const std::string& in_unique_identifier) {
  StopSessionWithReason(
      static_cast<uint32_t>(SessionStopReason::REQUEST_FROM_SESSION_MANAGER));
}

void SessionManagerImpl::StopSessionWithReason(uint32_t reason) {
  LOG(INFO) << "Stopping all sessions reason = " << reason;
  // Most calls to StopSession() will log the reason for the call.
  // If you don't see a log message saying the reason for the call, it is
  // likely a D-Bus message.
  manager_->ScheduleShutdown();
  // TODO(cmasone): re-enable these when we try to enable logout without exiting
  //                the session manager
  // browser_.job->StopSession();
  // user_policy_.reset();
  // session_started_ = false;

  password_provider_->DiscardPassword();
}

bool SessionManagerImpl::LoadShillProfile(brillo::ErrorPtr* error,
                                          const std::string& in_account_id) {
  LOG(INFO) << "LoadShillProfile() method called.";
  std::string actual_account_id;
  if (!NormalizeAccountId(in_account_id, &actual_account_id, error)) {
    DCHECK(*error);
    return false;
  }
  init_controller_->TriggerImpulse(kLoadShillProfileImpulse,
                                   {"CHROMEOS_USER=" + actual_account_id},
                                   InitDaemonController::TriggerMode::ASYNC);
  return true;
}

void SessionManagerImpl::StorePolicyEx(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response,
    const std::vector<uint8_t>& in_descriptor_blob,
    const std::vector<uint8_t>& in_policy_blob) {
  brillo::ErrorPtr error;
  PolicyDescriptor descriptor;
  if (!ParseAndValidatePolicyDescriptor(in_descriptor_blob,
                                        PolicyDescriptorUsage::kStore,
                                        &descriptor, &error)) {
    response->ReplyWithError(error.get());
    return;
  }

  PolicyService* policy_service = GetPolicyService(descriptor, &error);
  if (!policy_service) {
    response->ReplyWithError(error.get());
    return;
  }

  int key_flags = GetKeyInstallFlags(descriptor);
  PolicyNamespace ns(descriptor.domain(), descriptor.component_id());

  // If the blob is empty, return an error.
  DCHECK(dbus_service_);
  if (in_policy_blob.empty()) {
    auto error =
        CreateError(dbus_error::kInvalidParameter, "Empty policy provided");
    response->ReplyWithError(error.get());
    return;
  } else {
    policy_service->Store(ns, in_policy_blob, key_flags,
                          dbus_service_->CreatePolicyServiceCompletionCallback(
                              std::move(response)));
  }
}

bool SessionManagerImpl::RetrievePolicyEx(
    brillo::ErrorPtr* error,
    const std::vector<uint8_t>& in_descriptor_blob,
    std::vector<uint8_t>* out_policy_blob) {
  PolicyDescriptor descriptor;
  if (!ParseAndValidatePolicyDescriptor(in_descriptor_blob,
                                        PolicyDescriptorUsage::kRetrieve,
                                        &descriptor, error)) {
    LOG(ERROR) << kParseDescriptorFailMessage;
    *error =
        CreateError(dbus_error::kSigEncodeFail, kParseDescriptorFailMessage);
    return false;
  }

  PolicyService* policy_service = GetPolicyService(descriptor, error);
  if (!policy_service) {
    LOG(ERROR) << kGetPolicyServiceFailMessage;
    *error =
        CreateError(dbus_error::kSigEncodeFail, kGetPolicyServiceFailMessage);
    return false;
  }

  PolicyNamespace ns(descriptor.domain(), descriptor.component_id());

  if (!policy_service->Retrieve(ns, out_policy_blob)) {
    LOG(ERROR) << kSigEncodeFailMessage;
    *error = CreateError(dbus_error::kSigEncodeFail, kSigEncodeFailMessage);
    return false;
  }
  return true;
}

std::string SessionManagerImpl::RetrieveSessionState() {
  if (!session_started_) {
    return kStopped;
  }
  if (session_stopping_) {
    return kStopping;
  }
  return kStarted;
}

std::map<std::string, std::string>
SessionManagerImpl::RetrieveActiveSessions() {
  std::map<std::string, std::string> result;
  for (const auto& entry : user_sessions_) {
    if (!entry.second) {
      continue;
    }
    result[entry.second->username] = entry.second->userhash;
  }
  return result;
}

void SessionManagerImpl::RetrievePrimarySession(
    std::string* out_username, std::string* out_sanitized_username) {
  out_username->clear();
  out_sanitized_username->clear();
  if (user_sessions_.count(primary_user_account_id_) > 0) {
    out_username->assign(user_sessions_[primary_user_account_id_]->username);
    out_sanitized_username->assign(
        user_sessions_[primary_user_account_id_]->userhash);
  }
}

bool SessionManagerImpl::IsGuestSessionActive() {
  return !user_sessions_.empty() && AllSessionsAreIncognito();
}

bool SessionManagerImpl::LockScreen(brillo::ErrorPtr* error) {
  if (!session_started_) {
    *error = CREATE_WARNING_AND_LOG(
        dbus_error::kSessionDoesNotExist,
        "Attempt to lock screen outside of user session.");
    return false;
  }
  // If all sessions are incognito, then locking is not allowed.
  if (AllSessionsAreIncognito()) {
    *error =
        CREATE_WARNING_AND_LOG(dbus_error::kSessionExists,
                               "Attempt to lock screen during Guest session.");
    return false;
  }
  if (!screen_locked_) {
    screen_locked_ = true;
    init_controller_->TriggerImpulse(kScreenLockedImpulse, {},
                                     InitDaemonController::TriggerMode::ASYNC);
    delegate_->LockScreen();
  }
  LOG(INFO) << "LockScreen() method called.";
  return true;
}

void SessionManagerImpl::HandleLockScreenShown() {
  LOG(INFO) << "HandleLockScreenShown() method called.";
  adaptor_.SendScreenIsLockedSignal();
}

void SessionManagerImpl::HandleLockScreenDismissed() {
  screen_locked_ = false;
  init_controller_->TriggerImpulse(kScreenUnlockedImpulse, {},
                                   InitDaemonController::TriggerMode::ASYNC);
  LOG(INFO) << "HandleLockScreenDismissed() method called.";
  adaptor_.SendScreenIsUnlockedSignal();
}

bool SessionManagerImpl::IsScreenLocked() {
  return screen_locked_;
}

bool SessionManagerImpl::RestartJob(brillo::ErrorPtr* error,
                                    const base::ScopedFD& in_cred_fd,
                                    const std::vector<std::string>& in_argv,
                                    uint32_t mode) {
  struct ucred ucred = {0};
  socklen_t len = sizeof(struct ucred);
  if (!in_cred_fd.is_valid() || getsockopt(in_cred_fd.get(), SOL_SOCKET,
                                           SO_PEERCRED, &ucred, &len) == -1) {
    PLOG(ERROR) << "Can't get peer creds";
    *error = CreateError(dbus_error::kGetPeerCredsFailed, strerror(errno));
    return false;
  }

  if (!manager_->IsBrowser(ucred.pid)) {
    *error = CREATE_ERROR_AND_LOG(dbus_error::kUnknownPid,
                                  "Provided pid is unknown.");
    return false;
  }

  if (IsGuestMode(mode) != IsGuestSession(in_argv)) {
    *error =
        CREATE_ERROR_AND_LOG(dbus_error::kInvalidParameter,
                             "in_argv doesn't match mode for guest session.");
    return false;
  }

  // To set "logged-in" state for BWSI mode.
  if (IsGuestMode(mode) && !StartSession(error, *GetGuestUsername(), "")) {
    DCHECK(*error);
    return false;
  }

  if (!IsGuestMode(mode) && IsSessionStarted()) {
    *error =
        CREATE_ERROR_AND_LOG(dbus_error::kInvalidParameter,
                             "Requested to restart non-guest user session.");
    return false;
  }

  manager_->SetBrowserArgs(in_argv);
  manager_->RestartBrowser();
  return true;
}

bool SessionManagerImpl::StartDeviceWipe(brillo::ErrorPtr* error) {
  if (system_utils_->Exists(base::FilePath(kLoggedInFlag))) {
    *error = CREATE_ERROR_AND_LOG(dbus_error::kSessionExists,
                                  "A user has already logged in this boot.");
    return false;
  }

  InitiateDeviceWipe("session_manager_dbus_request");
  return true;
}

bool SessionManagerImpl::StartRemoteDeviceWipe(
    brillo::ErrorPtr* error, const std::vector<uint8_t>& signed_command) {
  if (!device_policy_->ValidateRemoteDeviceWipeCommand(
          signed_command,
          enterprise_management::PolicyFetchRequest_SignatureType_SHA256_RSA)) {
    *error = CreateError(dbus_error::kInvalidParameter,
                         "Invalid remote device wipe command signature type.");

    return false;
  }

  InitiateDeviceWipe("remote_wipe_request");

  return true;
}

void SessionManagerImpl::ClearBlockDevmodeVpd(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response) {
  device_policy_->ClearBlockDevmode(
      dbus_service_->CreatePolicyServiceCompletionCallback(
          std::move(response)));
}

bool SessionManagerImpl::StartTPMFirmwareUpdate(
    brillo::ErrorPtr* error, const std::string& update_mode) {
  // Make sure |update_mode| is supported.
  if (update_mode != kTPMFirmwareUpdateModeFirstBoot &&
      update_mode != kTPMFirmwareUpdateModePreserveStateful &&
      update_mode != kTPMFirmwareUpdateModeCleanup) {
    *error =
        CREATE_ERROR_AND_LOG(dbus_error::kInvalidParameter, "Bad update mode.");
    return false;
  }

  // Verify that we haven't seen a user log in since boot.
  if (system_utils_->Exists(base::FilePath(kLoggedInFlag))) {
    *error = CREATE_ERROR_AND_LOG(dbus_error::kSessionExists,
                                  "A user has already logged in since boot.");
    return false;
  }

  // For remotely managed devices, make sure the requested update mode matches
  // the admin-configured one in device policy.
  if (install_attributes_reader_->GetAttribute(
          InstallAttributesReader::kAttrMode) ==
      InstallAttributesReader::kDeviceModeEnterprise) {
    const enterprise_management::TPMFirmwareUpdateSettingsProto& settings =
        device_policy_->GetSettings().tpm_firmware_update_settings();
    std::set<std::string> allowed_modes;
    if (settings.allow_user_initiated_powerwash()) {
      allowed_modes.insert(kTPMFirmwareUpdateModeFirstBoot);
    }
    if (settings.allow_user_initiated_preserve_device_state()) {
      allowed_modes.insert(kTPMFirmwareUpdateModePreserveStateful);
    }

    // See whether the requested mode is allowed. Cleanup is permitted when at
    // least one of the actual modes are allowed.
    bool allowed = (update_mode == kTPMFirmwareUpdateModeCleanup)
                       ? !allowed_modes.empty()
                       : allowed_modes.count(update_mode) > 0;
    if (!allowed) {
      *error = CreateError(dbus_error::kNotAvailable,
                           "Policy doesn't allow TPM firmware update.");
      return false;
    }
  }

  // Validate that a firmware update is actually available to make sure
  // enterprise users can't abuse TPM firmware update to trigger powerwash.
  bool available = false;
  if (update_mode == kTPMFirmwareUpdateModeFirstBoot ||
      update_mode == kTPMFirmwareUpdateModePreserveStateful) {
    std::string update_location;
    available =
        system_utils_->ReadFileToString(
            base::FilePath(kTPMFirmwareUpdateLocationFile), &update_location) &&
        update_location.size();
  } else if (update_mode == kTPMFirmwareUpdateModeCleanup) {
    available = system_utils_->Exists(
        base::FilePath(kTPMFirmwareUpdateSRKVulnerableROCAFile));
  }

  if (!available) {
    *error =
        CREATE_ERROR_AND_LOG(dbus_error::kNotAvailable, "No update available.");
    return false;
  }

  // Put the update request into place.
  if (!system_utils_->WriteFileAtomically(
          base::FilePath(kTPMFirmwareUpdateRequestFlagFile),
          base::as_byte_span(update_mode), S_IRUSR | S_IWUSR | S_IROTH,
          {.uid = 0, .gid = 0})) {
    *error = CREATE_ERROR_AND_LOG(dbus_error::kNotAvailable,
                                  "Failed to persist update request.");
    return false;
  }

  if (update_mode == kTPMFirmwareUpdateModeFirstBoot ||
      update_mode == kTPMFirmwareUpdateModeCleanup) {
    InitiateDeviceWipe("tpm_firmware_update_" + update_mode);
  } else if (update_mode == kTPMFirmwareUpdateModePreserveStateful) {
    // This flag file indicates that encrypted stateful should be preserved.
    if (!system_utils_->WriteFileAtomically(
            base::FilePath(kStatefulPreservationRequestFile),
            base::as_byte_span(update_mode), S_IRUSR | S_IWUSR | S_IROTH,
            {.uid = 0, .gid = 0})) {
      *error = CREATE_ERROR_AND_LOG(dbus_error::kNotAvailable,
                                    "Failed to request stateful preservation.");
      return false;
    }

    if (!crossystem_->VbSetSystemPropertyInt(
            crossystem::Crossystem::kClearTpmOwnerRequest, 1)) {
      *error = CREATE_ERROR_AND_LOG(dbus_error::kNotAvailable,
                                    "Failed to request TPM clear.");
      return false;
    }

    RestartDevice("tpm_firmware_update " + update_mode);
  } else {
    NOTREACHED_IN_MIGRATION();
    return false;
  }

  return true;
}

void SessionManagerImpl::SetFlagsForUser(
    const std::string& in_account_id,
    const std::vector<std::string>& in_flags) {
  manager_->SetFlagsForUser(in_account_id, in_flags);
}

void SessionManagerImpl::SetFeatureFlagsForUser(
    const std::string& in_account_id,
    const std::vector<std::string>& in_feature_flags,
    const std::map<std::string, std::string>& in_origin_list_flags) {
  manager_->SetFeatureFlagsForUser(in_account_id, in_feature_flags,
                                   in_origin_list_flags);
}

void SessionManagerImpl::GetServerBackedStateKeys(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        std::vector<std::vector<uint8_t>>>> response) {
  DCHECK(dbus_service_);
  DeviceIdentifierGenerator::StateKeyCallback callback =
      dbus_service_->CreateStateKeyCallback(std::move(response));
  if (system_clock_synchronized_) {
    device_identifier_generator_->RequestStateKeys(std::move(callback));
  } else {
    pending_state_key_callbacks_.push_back(std::move(callback));
  }
}

void SessionManagerImpl::GetPsmDeviceActiveSecret(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::string>>
        response) {
  DCHECK(dbus_service_);
  DeviceIdentifierGenerator::PsmDeviceActiveSecretCallback callback =
      dbus_service_->CreatePsmDeviceActiveSecretCallback(std::move(response));
  device_identifier_generator_->RequestPsmDeviceActiveSecret(
      std::move(callback));
}

void SessionManagerImpl::OnSuspendImminent(dbus::Signal* signal) {
  suspend_ongoing_ = true;

  // If Chrome crashed recently, it might've missed this SuspendImminent signal
  // and failed to lock the screen. Stop the session as a precaution:
  // https://crbug.com/867970.
  const base::TimeTicks start_time = manager_->GetLastBrowserRestartTime();
  if (!start_time.is_null() &&
      tick_clock_->NowTicks() - start_time <= kCrashBeforeSuspendInterval) {
    LOG(INFO) << "Stopping session for suspend after recent browser restart";
    StopSessionWithReason(
        static_cast<uint32_t>(SessionStopReason::SUSPEND_AFTER_RESTART));
  }
}

void SessionManagerImpl::OnSuspendDone(dbus::Signal* signal) {
  suspend_ongoing_ = false;
  last_suspend_done_time_ = tick_clock_->NowTicks();
}

void SessionManagerImpl::OnSystemClockServiceAvailable(bool service_available) {
  if (!service_available) {
    LOG(ERROR) << "Failed to listen for tlsdated service start";
    return;
  }

  GetSystemClockLastSyncInfo();
}

void SessionManagerImpl::GetSystemClockLastSyncInfo() {
  dbus::MethodCall method_call(system_clock::kSystemClockInterface,
                               system_clock::kSystemLastSyncInfo);
  system_clock_proxy_->CallMethod(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
      base::BindOnce(&SessionManagerImpl::OnGotSystemClockLastSyncInfo,
                     weak_ptr_factory_.GetWeakPtr()));
}

void SessionManagerImpl::OnGotSystemClockLastSyncInfo(
    dbus::Response* response) {
  if (!response) {
    LOG(ERROR) << system_clock::kSystemClockInterface << "."
               << system_clock::kSystemLastSyncInfo << " request failed.";
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&SessionManagerImpl::GetSystemClockLastSyncInfo,
                       weak_ptr_factory_.GetWeakPtr()),
        system_clock_last_sync_info_retry_delay_);
    return;
  }

  dbus::MessageReader reader(response);
  bool network_synchronized = false;
  if (!reader.PopBool(&network_synchronized)) {
    LOG(ERROR) << system_clock::kSystemClockInterface << "."
               << system_clock::kSystemLastSyncInfo
               << " response lacks network-synchronized argument";
    return;
  }

  if (network_synchronized) {
    system_clock_synchronized_ = true;
    for (auto& callback : pending_state_key_callbacks_) {
      device_identifier_generator_->RequestStateKeys(std::move(callback));
    }
    pending_state_key_callbacks_.clear();
  } else {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&SessionManagerImpl::GetSystemClockLastSyncInfo,
                       weak_ptr_factory_.GetWeakPtr()),
        system_clock_last_sync_info_retry_delay_);
  }
}

bool SessionManagerImpl::InitMachineInfo(brillo::ErrorPtr* error,
                                         const std::string& in_data) {
  std::map<std::string, std::string> params;

  vpd::Vpd vpd;
  const auto ro_vpd = vpd.GetValues(vpd::VpdRo);

  if (!DeviceIdentifierGenerator::ParseMachineInfo(in_data, ro_vpd, &params)) {
    *error = CreateError(dbus_error::kInitMachineInfoFail, "Parse failure.");
    return false;
  }

  if (!device_identifier_generator_->InitMachineInfo(params)) {
    *error =
        CreateError(dbus_error::kInitMachineInfoFail, "Missing parameters.");
    return false;
  }
  return true;
}

bool SessionManagerImpl::StartArcMiniContainer(
    brillo::ErrorPtr* error, const std::vector<uint8_t>& in_request) {
  return arc_manager_->StartArcMiniContainer(error, in_request);
}

bool SessionManagerImpl::UpgradeArcContainer(
    brillo::ErrorPtr* error, const std::vector<uint8_t>& in_request) {
  return arc_manager_->UpgradeArcContainer(error, in_request);
}

bool SessionManagerImpl::StopArcInstance(brillo::ErrorPtr* error,
                                         const std::string& account_id,
                                         bool should_backup_log) {
  return arc_manager_->StopArcInstance(error, account_id, should_backup_log);
}

bool SessionManagerImpl::SetArcCpuRestriction(brillo::ErrorPtr* error,
                                              uint32_t in_restriction_state) {
  return arc_manager_->SetArcCpuRestriction(error, in_restriction_state);
}

bool SessionManagerImpl::EmitArcBooted(brillo::ErrorPtr* error,
                                       const std::string& in_account_id) {
  return arc_manager_->EmitArcBooted(error, in_account_id);
}

bool SessionManagerImpl::GetArcStartTimeTicks(brillo::ErrorPtr* error,
                                              int64_t* out_start_time) {
  return arc_manager_->GetArcStartTimeTicks(error, out_start_time);
}

void SessionManagerImpl::EnableAdbSideload(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  arc_manager_->EnableAdbSideload(std::move(response));
}

void SessionManagerImpl::QueryAdbSideload(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  arc_manager_->QueryAdbSideload(std::move(response));
}

void SessionManagerImpl::OnPolicyPersisted(bool success) {
  LOG(INFO) << "Policy persisted result: " << success;
  device_local_account_manager_->UpdateDeviceSettings(
      device_policy_->GetSettings());
  adaptor_.SendPropertyChangeCompleteSignal(ToSuccessSignal(success));
}

void SessionManagerImpl::OnKeyPersisted(bool success) {
  adaptor_.SendSetOwnerKeyCompleteSignal(ToSuccessSignal(success));
}

void SessionManagerImpl::InitiateDeviceWipe(const std::string& reason) {
  // The log string must not be confused with other clobbers-state parameters.
  // Sanitize by replacing all non-alphanumeric characters with underscores and
  // clamping size to 50 characters.
  std::string sanitized_reason(reason.substr(0, 50));
  std::locale locale("C");
  std::replace_if(
      sanitized_reason.begin(), sanitized_reason.end(),
      [&locale](const std::string::value_type character) {
        return !std::isalnum(character, locale);
      },
      '_');
  const base::FilePath reset_path(kResetFile);
  const std::string reset_file_content =
      "fast safe keepimg preserve_lvs reason=" + sanitized_reason;
  system_utils_->WriteFileAtomically(
      reset_path, base::as_byte_span(reset_file_content),
      S_IRUSR | S_IWUSR | S_IROTH, {.uid = 0, .gid = 0});

  RestartDevice(sanitized_reason);
}

// static
bool SessionManagerImpl::NormalizeAccountId(const std::string& account_id,
                                            std::string* actual_account_id_out,
                                            brillo::ErrorPtr* error_out) {
  if (ValidateAccountId(account_id, actual_account_id_out)) {
    DCHECK(!actual_account_id_out->empty());
    return true;
  }

  // TODO(alemate): adjust this error message after ChromeOS will stop using
  // email as cryptohome identifier.
  *error_out =
      CREATE_ERROR_AND_LOG(dbus_error::kInvalidAccount,
                           "Provided email address is not valid.  ASCII only.");
  DCHECK(actual_account_id_out->empty());
  return false;
}

bool SessionManagerImpl::AllSessionsAreIncognito() {
  size_t incognito_count = 0;
  for (UserSessionMap::const_iterator it = user_sessions_.begin();
       it != user_sessions_.end(); ++it) {
    if (it->second) {
      incognito_count += it->second->is_incognito;
    }
  }
  return incognito_count == user_sessions_.size();
}

std::unique_ptr<SessionManagerImpl::UserSession>
SessionManagerImpl::CreateUserSession(const std::string& username,
                                      bool is_incognito,
                                      brillo::ErrorPtr* error) {
  std::unique_ptr<PolicyService> user_policy =
      user_policy_factory_->Create(username);
  if (!user_policy) {
    LOG(ERROR) << "User policy failed to initialize.";
    *error = CreateError(dbus_error::kPolicyInitFail, "Can't create session.");
    return nullptr;
  }

  return std::make_unique<UserSession>(username,
                                       *SanitizeUserName(Username(username)),
                                       is_incognito, std::move(user_policy));
}

PolicyService* SessionManagerImpl::GetPolicyService(
    const PolicyDescriptor& descriptor, brillo::ErrorPtr* error) {
  DCHECK(error);
  PolicyService* policy_service = nullptr;
  switch (descriptor.account_type()) {
    case ACCOUNT_TYPE_DEVICE: {
      policy_service = device_policy_.get();
      break;
    }
    case ACCOUNT_TYPE_USER: {
      UserSessionMap::const_iterator it =
          user_sessions_.find(descriptor.account_id());
      policy_service = it != user_sessions_.end()
                           ? it->second->policy_service.get()
                           : nullptr;
      break;
    }
    case ACCOUNT_TYPE_DEVICE_LOCAL_ACCOUNT: {
      policy_service = device_local_account_manager_->GetPolicyService(
          descriptor.account_id());
      break;
    }
  }
  if (policy_service) {
    return policy_service;
  }

  // Error case
  const std::string message =
      base::StringPrintf("Cannot get policy service for account type %i",
                         static_cast<int>(descriptor.account_type()));
  LOG(ERROR) << message;
  *error = CreateError(dbus_error::kGetServiceFail, message);
  return nullptr;
}

bool SessionManagerImpl::OwnerIsSignedIn() {
  CHECK(device_policy_);
  for (auto& [account_id, session] : user_sessions_) {
    if (device_policy_->UserIsOwner(account_id)) {
      return true;
    }
  }
  return false;
}

int SessionManagerImpl::GetKeyInstallFlags(const PolicyDescriptor& descriptor) {
  switch (descriptor.account_type()) {
    case ACCOUNT_TYPE_DEVICE: {
      // It's safe to always allow rotation because the new key is signed with
      // the old one.
      int flags = PolicyService::KEY_ROTATE;
      // The first non-guest user is supposed to install a new key.
      // Alternatively, cloud managed devices can receive policies before any
      // sessions started and install the key from them.
      if (!AllSessionsAreIncognito() || !session_started_) {
        flags |= PolicyService::KEY_INSTALL_NEW;
      }
      // If the owner is signed in, then allow clobbering the key. Also allow
      // clobbering on the login screen where ChromeOS is presumably in a more
      // secure state (primarily for managed devices).
      if (OwnerIsSignedIn() || !session_started_) {
        flags |= PolicyService::KEY_CLOBBER;
      }
      return flags;
    }
    case ACCOUNT_TYPE_USER:
      return PolicyService::KEY_INSTALL_NEW | PolicyService::KEY_ROTATE;
    case ACCOUNT_TYPE_DEVICE_LOCAL_ACCOUNT:
      return PolicyService::KEY_NONE;
  }
}

void SessionManagerImpl::RestartDevice(const std::string& reason) {
  delegate_->RestartDevice("session_manager (" + reason + ")");
}

bool SessionManagerImpl::IsSessionStarted() {
  return !user_sessions_.empty();
}

void SessionManagerImpl::OnArcInstanceStopped(uint32_t value) {
  adaptor_.SendArcInstanceStoppedSignal(value);
}

}  // namespace login_manager
