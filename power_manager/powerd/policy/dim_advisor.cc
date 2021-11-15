// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/dim_advisor.h"

#include <string>
#include <utility>

#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>

#include "power_manager/powerd/policy/state_controller.h"

namespace power_manager {
namespace policy {
namespace {

// Timeout for RequestSmartDimDecision.
static constexpr base::TimeDelta kSmartDimDecisionTimeout =
    base::TimeDelta::FromSeconds(3);

}  // namespace

DimAdvisor::DimAdvisor() : weak_ptr_factory_(this) {}

DimAdvisor::~DimAdvisor() {
  if (dbus_wrapper_)
    dbus_wrapper_->RemoveObserver(this);
}

void DimAdvisor::Init(system::DBusWrapperInterface* dbus_wrapper,
                      StateController* state_controller) {
  state_controller_ = state_controller;
  dbus_wrapper_ = dbus_wrapper;
  dbus_wrapper_->AddObserver(this);
  ml_decision_dbus_proxy_ = dbus_wrapper->GetObjectProxy(
      chromeos::kMlDecisionServiceName, chromeos::kMlDecisionServicePath);
  dbus_wrapper->RegisterForServiceAvailability(
      ml_decision_dbus_proxy_,
      base::BindOnce(&DimAdvisor::HandleMlDecisionServiceAvailableOrRestarted,
                     weak_ptr_factory_.GetWeakPtr()));
  hps_dbus_proxy_ =
      dbus_wrapper->GetObjectProxy(hps::kHpsServiceName, hps::kHpsServicePath);
  dbus_wrapper->RegisterForSignal(
      hps_dbus_proxy_, hps::kHpsServiceInterface, hps::kHpsSenseChanged,
      base::BindRepeating(&DimAdvisor::HandleHpsSenseSignal,
                          weak_ptr_factory_.GetWeakPtr()));
}

bool DimAdvisor::ReadyForSmartDimRequest(
    base::TimeTicks now, base::TimeDelta screen_dim_imminent_delay) const {
  return IsSmartDimEnabled() && !waiting_for_smart_dim_decision_ &&
         now - last_smart_dim_decision_request_time_ >=
             screen_dim_imminent_delay;
}

void DimAdvisor::RequestSmartDimDecision(base::TimeTicks now) {
  waiting_for_smart_dim_decision_ = true;
  last_smart_dim_decision_request_time_ = now;

  dbus::MethodCall method_call(
      chromeos::kMlDecisionServiceInterface,
      chromeos::kMlDecisionServiceShouldDeferScreenDimMethod);

  dbus_wrapper_->CallMethodAsync(
      ml_decision_dbus_proxy_, &method_call, kSmartDimDecisionTimeout,
      base::BindOnce(&DimAdvisor::HandleSmartDimResponse,
                     weak_ptr_factory_.GetWeakPtr()));
}

bool DimAdvisor::IsSmartDimEnabled() const {
  return ml_decision_service_available_;
}

bool DimAdvisor::IsHpsSenseEnabled() const {
  return hps_sense_connected_;
}

void DimAdvisor::OnDBusNameOwnerChanged(const std::string& service_name,
                                        const std::string& old_owner,
                                        const std::string& new_owner) {
  if (service_name == chromeos::kMlDecisionServiceName && !new_owner.empty()) {
    LOG(INFO) << "D-Bus " << service_name << " ownership changed to "
              << new_owner;
    HandleMlDecisionServiceAvailableOrRestarted(true);
  }
}

void DimAdvisor::HandleMlDecisionServiceAvailableOrRestarted(bool available) {
  ml_decision_service_available_ = available;
  if (!available) {
    LOG(ERROR) << "Failed waiting for ml decision service to become "
                  "available";
    return;
  }
}

void DimAdvisor::HandleSmartDimResponse(dbus::Response* response) {
  DCHECK(waiting_for_smart_dim_decision_)
      << "Smart dim decision is not being waited for";

  waiting_for_smart_dim_decision_ = false;

  if (!response) {
    LOG(ERROR) << "D-Bus method call to "
               << chromeos::kMlDecisionServiceInterface << "."
               << chromeos::kMlDecisionServiceShouldDeferScreenDimMethod
               << " failed";
    return;
  }

  dbus::MessageReader reader(response);
  bool should_defer_screen_dim = false;
  if (!reader.PopBool(&should_defer_screen_dim)) {
    LOG(ERROR) << "Unable to read info from "
               << chromeos::kMlDecisionServiceInterface << "."
               << chromeos::kMlDecisionServiceShouldDeferScreenDimMethod
               << " response";
    return;
  }

  if (!should_defer_screen_dim) {
    VLOG(1) << "Smart dim decided not to defer screen dimming";
    return;
  }

  LOG(INFO) << "Smart dim decided to defer screen dimming";
  state_controller_->HandleDeferFromSmartDim();
}

void DimAdvisor::HandleHpsSenseSignal(dbus::Signal* signal) {
  // Hps sense is considered connected as soon as we get one signal from it.
  // Otherwise it maybe disabled inside HpsService.
  hps_sense_connected_ = true;

  dbus::MessageReader reader(signal);
  bool value = false;

  if (!reader.PopBool(&value)) {
    LOG(ERROR) << "Can't read dbus signal from " << hps::kHpsServiceInterface
               << "." << hps::kHpsSenseChanged;
    return;
  }

  HpsResult hps_result = value ? HpsResult::POSITIVE : HpsResult::NEGATIVE;

  // Calls StateController::HandleHpsResultChange to consume new hps result.
  state_controller_->HandleHpsResultChange(hps_result);
}

void DimAdvisor::UnDimFeedback(base::TimeDelta dim_duration) {
  // TODO(charleszhao): Update the DimAdvisor based on the feedback.
}

}  // namespace policy
}  // namespace power_manager
