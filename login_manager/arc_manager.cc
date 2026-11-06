// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/arc_manager.h"

#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/memory/raw_ref.h>
#include <base/types/expected.h>
#include <brillo/dbus/dbus_method_response.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/errors/error.h>
#include <dbus/arc_manager/dbus-constants.h>
#include <dbus/bootlockbox/dbus-constants.h>
#include <dbus/bus.h>
#include <dbus/debugd/dbus-constants.h>
#include <dbus/error.h>
#include <dbus/login_manager/dbus-constants.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>

#include "login_manager/arc_sideload_status_interface.h"
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

ArcManager::ArcManager(SystemUtils& system_utils, scoped_refptr<dbus::Bus> bus)
    : ArcManager(
          system_utils,
          bus,
          std::make_unique<InitDaemonControllerImpl>(bus->GetObjectProxy(
              InitDaemonControllerImpl::kServiceName,
              dbus::ObjectPath(InitDaemonControllerImpl::kPath))),
          bus->GetObjectProxy(debugd::kDebugdServiceName,
                              dbus::ObjectPath(debugd::kDebugdServicePath)),
          CreateArcSideloadStatus(*bus)) {}

ArcManager::ArcManager(
    SystemUtils& system_utils,
    scoped_refptr<dbus::Bus> bus,
    std::unique_ptr<InitDaemonController> init_controller,
    dbus::ObjectProxy* debugd_proxy,
    std::unique_ptr<ArcSideloadStatusInterface> arc_sideload_status)
    : system_utils_(system_utils),
      bus_(bus),
      init_controller_(std::move(init_controller)),
      debugd_proxy_(debugd_proxy),
      arc_sideload_status_(std::move(arc_sideload_status)) {}

ArcManager::~ArcManager() = default;

std::unique_ptr<ArcManager> ArcManager::CreateForTesting(
    SystemUtils& system_utils,
    scoped_refptr<dbus::Bus> bus,
    std::unique_ptr<InitDaemonController> init_controller,
    dbus::ObjectProxy* debugd_proxy,
    std::unique_ptr<ArcSideloadStatusInterface> arc_sideload_status) {
  // std::make_unique won't work because the target ctor is private.
  return base::WrapUnique(
      new ArcManager(system_utils, bus, std::move(init_controller),
                     debugd_proxy, std::move(arc_sideload_status)));
}

void ArcManager::Initialize() {
  arc_sideload_status_->Initialize();
}

void ArcManager::Finalize() {
  dbus_service_.reset();

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

void ArcManager::EmitStopArcVmInstanceImpulse() {
  if (!init_controller_->TriggerImpulse(
          kStopArcVmInstanceImpulse, {},
          InitDaemonController::TriggerMode::SYNC)) {
    LOG(ERROR) << "Emitting stop-arcvm-instance impulse failed.";
  }
}

void ArcManager::OnUserSessionStarted(const std::string& in_account_id) {
  DeleteArcBugReportBackup(in_account_id);
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
