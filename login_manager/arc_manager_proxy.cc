// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/arc_manager_proxy.h"

#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
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
    : arc_manager_(arc_manager) {
  observation_.Observe(&arc_manager);
}

ArcManagerProxyInProcess::~ArcManagerProxyInProcess() = default;

void ArcManagerProxyInProcess::AddObserver(
    ArcManagerProxy::Observer* observer) {
  observers_.AddObserver(observer);
}

void ArcManagerProxyInProcess::RemoveObserver(
    ArcManagerProxy::Observer* observer) {
  observers_.RemoveObserver(observer);
}

bool ArcManagerProxyInProcess::OnUserSessionStarted(
    const std::string& in_account_id) {
  arc_manager_->OnUserSessionStarted(in_account_id);
  return true;
}

bool ArcManagerProxyInProcess::EmitStopArcVmInstanceImpulse() {
  arc_manager_->EmitStopArcVmInstanceImpulse();
  return true;
}

bool ArcManagerProxyInProcess::RequestJobExit(uint32_t reason) {
  arc_manager_->RequestJobExit(reason);
  return true;
}

bool ArcManagerProxyInProcess::EnsureJobExit(int64_t timeout_ms) {
  arc_manager_->EnsureJobExit(timeout_ms);
  return true;
}

bool ArcManagerProxyInProcess::StartArcMiniContainer(
    brillo::ErrorPtr* error, const std::vector<uint8_t>& in_request) {
  return arc_manager_->StartArcMiniContainer(error, in_request);
}

bool ArcManagerProxyInProcess::UpgradeArcContainer(
    brillo::ErrorPtr* error, const std::vector<uint8_t>& in_request) {
  return arc_manager_->UpgradeArcContainer(error, in_request);
}

bool ArcManagerProxyInProcess::StopArcInstance(brillo::ErrorPtr* error,
                                               const std::string& in_account_id,
                                               bool in_should_backup_log) {
  return arc_manager_->StopArcInstance(error, in_account_id,
                                       in_should_backup_log);
}

bool ArcManagerProxyInProcess::SetArcCpuRestriction(
    brillo::ErrorPtr* error, uint32_t in_restriction_state) {
  return arc_manager_->SetArcCpuRestriction(error, in_restriction_state);
}

bool ArcManagerProxyInProcess::EmitArcBooted(brillo::ErrorPtr* error,
                                             const std::string& in_account_id) {
  return arc_manager_->EmitArcBooted(error, in_account_id);
}

bool ArcManagerProxyInProcess::GetArcStartTimeTicks(brillo::ErrorPtr* error,
                                                    int64_t* out_start_time) {
  return arc_manager_->GetArcStartTimeTicks(error, out_start_time);
}

void ArcManagerProxyInProcess::EnableAdbSideload(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  arc_manager_->EnableAdbSideload(std::move(response));
}

void ArcManagerProxyInProcess::QueryAdbSideload(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  arc_manager_->QueryAdbSideload(std::move(response));
}

void ArcManagerProxyInProcess::OnArcInstanceStopped(uint32_t value) {
  for (auto& observer : observers_) {
    observer.OnArcInstanceStopped(value);
  }
}

ArcManagerProxyDBus::ArcManagerProxyDBus(scoped_refptr<dbus::Bus> bus)
    : arc_manager_(bus) {
  arc_manager_.RegisterArcInstanceStoppedSignalHandler(
      base::BindRepeating(&ArcManagerProxyDBus::OnArcInstanceStopped,
                          base::Unretained(this)),
      base::DoNothing());
}

ArcManagerProxyDBus::~ArcManagerProxyDBus() = default;

void ArcManagerProxyDBus::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void ArcManagerProxyDBus::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

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

bool ArcManagerProxyDBus::RequestJobExit(uint32_t reason) {
  brillo::ErrorPtr error;
  bool result = arc_manager_.RequestJobExit(reason, &error);
  LOG_IF(ERROR, error) << "ArcManagerProxyDBus::RequestJobExit: " << *error;
  return result;
}

bool ArcManagerProxyDBus::EnsureJobExit(int64_t timeout_ms) {
  brillo::ErrorPtr error;
  bool result = arc_manager_.EnsureJobExit(timeout_ms, &error);
  LOG_IF(ERROR, error) << "ArcManagerProxyDBus::EnsureJobExit: " << *error;
  return result;
}

bool ArcManagerProxyDBus::StartArcMiniContainer(
    brillo::ErrorPtr* error, const std::vector<uint8_t>& in_request) {
  return arc_manager_.StartArcMiniContainer(in_request, error);
}

bool ArcManagerProxyDBus::UpgradeArcContainer(
    brillo::ErrorPtr* error, const std::vector<uint8_t>& in_request) {
  return arc_manager_.UpgradeArcContainer(in_request, error);
}

bool ArcManagerProxyDBus::StopArcInstance(brillo::ErrorPtr* error,
                                          const std::string& in_account_id,
                                          bool in_should_backup_log) {
  return arc_manager_.StopArcInstance(in_account_id, in_should_backup_log,
                                      error);
}

bool ArcManagerProxyDBus::SetArcCpuRestriction(brillo::ErrorPtr* error,
                                               uint32_t in_restriction_state) {
  return arc_manager_.SetArcCpuRestriction(in_restriction_state, error);
}

bool ArcManagerProxyDBus::EmitArcBooted(brillo::ErrorPtr* error,
                                        const std::string& in_account_id) {
  return arc_manager_.EmitArcBooted(in_account_id, error);
}

bool ArcManagerProxyDBus::GetArcStartTimeTicks(brillo::ErrorPtr* error,
                                               int64_t* out_start_time) {
  return arc_manager_.GetArcStartTimeTicks(out_start_time, error);
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

void ArcManagerProxyDBus::OnArcInstanceStopped(uint32_t value) {
  for (auto& observer : observers_) {
    observer.OnArcInstanceStopped(value);
  }
}

}  // namespace login_manager
