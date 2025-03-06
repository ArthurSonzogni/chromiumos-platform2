// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/arc_manager_proxy.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace login_manager {

void ArcManagerProxyBase::OnUserSessionStarted(
    const std::string& in_account_id) {
  arc_manager_->OnUserSessionStarted(in_account_id);
}

void ArcManagerProxyBase::EmitStopArcVmInstanceImpulse() {
  arc_manager_->EmitStopArcVmInstanceImpulse();
}

void ArcManagerProxyBase::RequestJobExit(uint32_t reason) {
  arc_manager_->RequestJobExit(reason);
}

void ArcManagerProxyBase::EnsureJobExit(int64_t timeout_ms) {
  arc_manager_->EnsureJobExit(timeout_ms);
}

bool ArcManagerProxyBase::StartArcMiniContainer(
    brillo::ErrorPtr* error, const std::vector<uint8_t>& in_request) {
  return arc_manager_->StartArcMiniContainer(error, in_request);
}

bool ArcManagerProxyBase::UpgradeArcContainer(
    brillo::ErrorPtr* error, const std::vector<uint8_t>& in_request) {
  return arc_manager_->UpgradeArcContainer(error, in_request);
}

bool ArcManagerProxyBase::StopArcInstance(brillo::ErrorPtr* error,
                                          const std::string& in_account_id,
                                          bool in_should_backup_log) {
  return arc_manager_->StopArcInstance(error, in_account_id,
                                       in_should_backup_log);
}

bool ArcManagerProxyBase::SetArcCpuRestriction(brillo::ErrorPtr* error,
                                               uint32_t in_restriction_state) {
  return arc_manager_->SetArcCpuRestriction(error, in_restriction_state);
}

bool ArcManagerProxyBase::EmitArcBooted(brillo::ErrorPtr* error,
                                        const std::string& in_account_id) {
  return arc_manager_->EmitArcBooted(error, in_account_id);
}

bool ArcManagerProxyBase::GetArcStartTimeTicks(brillo::ErrorPtr* error,
                                               int64_t* out_start_time) {
  return arc_manager_->GetArcStartTimeTicks(error, out_start_time);
}

void ArcManagerProxyBase::EnableAdbSideload(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  arc_manager_->EnableAdbSideload(std::move(response));
}

void ArcManagerProxyBase::QueryAdbSideload(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  arc_manager_->QueryAdbSideload(std::move(response));
}

ArcManagerProxyBase::ArcManagerProxyBase(
    org::chromium::ArcManagerInterface& arc_manager)
    : arc_manager_(arc_manager) {}

ArcManagerProxyBase::~ArcManagerProxyBase() = default;

void ArcManagerProxyBase::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void ArcManagerProxyBase::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

ArcManagerProxyInProcess::ArcManagerProxyInProcess(ArcManager& arc_manager)
    : ArcManagerProxyBase(arc_manager) {
  observation_.Observe(&arc_manager);
}

ArcManagerProxyInProcess::~ArcManagerProxyInProcess() = default;

void ArcManagerProxyInProcess::OnArcInstanceStopped(uint32_t value) {
  for (auto& observer : observers_) {
    observer.OnArcInstanceStopped(value);
  }
}

}  // namespace login_manager
