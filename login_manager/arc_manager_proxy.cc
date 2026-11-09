// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/arc_manager_proxy.h"

#include <memory>
#include <ostream>
#include <string>
#include <utility>

#include <base/logging.h>
#include <brillo/errors/error.h>

namespace login_manager {
namespace {
std::ostream& operator<<(std::ostream& os, const brillo::Error& error) {
  return os << "{domain: " << error.GetDomain() << ", "
            << "code: " << error.GetCode() << ", "
            << "message: " << error.GetMessage() << "}";
}
}  // namespace

ArcManagerProxyInProcess::ArcManagerProxyInProcess(ArcManager& arc_manager)
    : arc_manager_(arc_manager) {}

ArcManagerProxyInProcess::~ArcManagerProxyInProcess() = default;

bool ArcManagerProxyInProcess::OnUserSessionStarted(
    const std::string& in_account_id) {
  arc_manager_->OnUserSessionStarted(in_account_id);
  return true;
}

bool ArcManagerProxyInProcess::EmitStopArcVmInstanceImpulse() {
  arc_manager_->EmitStopArcVmInstanceImpulse();
  return true;
}

bool ArcManagerProxyInProcess::EmitArcBooted(brillo::ErrorPtr* error,
                                             const std::string& in_account_id) {
  return arc_manager_->EmitArcBooted(error, in_account_id);
}

void ArcManagerProxyInProcess::EnableAdbSideload(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  arc_manager_->EnableAdbSideload(std::move(response));
}

void ArcManagerProxyInProcess::QueryAdbSideload(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  arc_manager_->QueryAdbSideload(std::move(response));
}

ArcManagerProxyDBus::ArcManagerProxyDBus(scoped_refptr<dbus::Bus> bus)
    : arc_manager_(bus) {}

ArcManagerProxyDBus::~ArcManagerProxyDBus() = default;

bool ArcManagerProxyDBus::OnUserSessionStarted(
    const std::string& in_account_id) {
  brillo::ErrorPtr error;
  bool result = arc_manager_.OnUserSessionStarted(in_account_id, &error);
  LOG_IF(ERROR, error) << "ArcManagerProxyDBus::OnUserSessionStarted: "
                       << *error;
  return result;
}

bool ArcManagerProxyDBus::EmitStopArcVmInstanceImpulse() {
  brillo::ErrorPtr error;
  bool result = arc_manager_.EmitStopArcVmInstanceImpulse(&error);
  LOG_IF(ERROR, error) << "ArcManagerProxyDBus::EmitStopArcVmInstanceImpulse: "
                       << *error;
  return result;
}

bool ArcManagerProxyDBus::EmitArcBooted(brillo::ErrorPtr* error,
                                        const std::string& in_account_id) {
  return arc_manager_.EmitArcBooted(in_account_id, error);
}

void ArcManagerProxyDBus::EnableAdbSideload(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  bool result = false;
  brillo::ErrorPtr error;
  if (arc_manager_.EnableAdbSideload(&result, &error)) {
    response->Return(result);
  } else {
    response->ReplyWithError(error.get());
  }
}

void ArcManagerProxyDBus::QueryAdbSideload(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  bool result = false;
  brillo::ErrorPtr error;
  if (arc_manager_.QueryAdbSideload(&result, &error)) {
    response->Return(result);
  } else {
    response->ReplyWithError(error.get());
  }
}

}  // namespace login_manager
