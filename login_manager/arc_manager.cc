// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/arc_manager.h"

#include <utility>

#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <brillo/dbus/dbus_method_response.h>
#include <brillo/errors/error.h>
#include <dbus/login_manager/dbus-constants.h>

#include "login_manager/arc_sideload_status_interface.h"
#include "login_manager/dbus_util.h"
#include "login_manager/system_utils.h"

namespace login_manager {
namespace {

constexpr char kLoggedInFlag[] = "/run/session_manager/logged_in";

}  // namespace

ArcManager::ArcManager(
    SystemUtils& system_utils,
    std::unique_ptr<ArcSideloadStatusInterface> arc_sideload_status)
    : system_utils_(system_utils),
      arc_sideload_status_(std::move(arc_sideload_status)) {}

ArcManager::~ArcManager() = default;

void ArcManager::Initialize() {
  arc_sideload_status_->Initialize();
}

void ArcManager::Finalize() {
  arc_sideload_status_.reset();
}

bool ArcManager::IsAdbSideloadAllowed() const {
  return arc_sideload_status_->IsAdbSideloadAllowed();
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

}  // namespace login_manager
