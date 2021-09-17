// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/smart_dim_requestor.h"

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

SmartDimRequestor::SmartDimRequestor() : weak_ptr_factory_(this) {}

SmartDimRequestor::~SmartDimRequestor() {
  if (dbus_wrapper_)
    dbus_wrapper_->RemoveObserver(this);
}

void SmartDimRequestor::Init(system::DBusWrapperInterface* dbus_wrapper,
                             StateController* state_controller) {
  state_controller_ = state_controller;
  dbus_wrapper_ = dbus_wrapper;
  dbus_wrapper_->AddObserver(this);
  ml_decision_dbus_proxy_ = dbus_wrapper->GetObjectProxy(
      chromeos::kMlDecisionServiceName, chromeos::kMlDecisionServicePath);
  dbus_wrapper->RegisterForServiceAvailability(
      ml_decision_dbus_proxy_,
      base::BindOnce(
          &SmartDimRequestor::HandleMlDecisionServiceAvailableOrRestarted,
          weak_ptr_factory_.GetWeakPtr()));
}

void SmartDimRequestor::HandleMlDecisionServiceAvailableOrRestarted(
    bool available) {
  ml_decision_service_available_ = available;
  if (!available) {
    LOG(ERROR) << "Failed waiting for ml decision service to become "
                  "available";
    return;
  }
}

void SmartDimRequestor::RequestSmartDimDecision(base::TimeTicks now) {
  waiting_for_smart_dim_decision_ = true;
  last_smart_dim_decision_request_time_ = now;

  dbus::MethodCall method_call(
      chromeos::kMlDecisionServiceInterface,
      chromeos::kMlDecisionServiceShouldDeferScreenDimMethod);

  dbus_wrapper_->CallMethodAsync(
      ml_decision_dbus_proxy_, &method_call, kSmartDimDecisionTimeout,
      base::BindOnce(&SmartDimRequestor::HandleSmartDimResponse,
                     weak_ptr_factory_.GetWeakPtr()));
}

bool SmartDimRequestor::ReadyForRequest(
    base::TimeTicks now, base::TimeDelta screen_dim_imminent_delay) {
  return IsEnabled() && !waiting_for_smart_dim_decision_ &&
         now - last_smart_dim_decision_request_time_ >=
             screen_dim_imminent_delay;
}

void SmartDimRequestor::HandleSmartDimResponse(dbus::Response* response) {
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

bool SmartDimRequestor::IsEnabled() {
  return ml_decision_service_available_;
}

void SmartDimRequestor::OnDBusNameOwnerChanged(const std::string& service_name,
                                               const std::string& old_owner,
                                               const std::string& new_owner) {
  if (service_name == chromeos::kMlDecisionServiceName && !new_owner.empty()) {
    LOG(INFO) << "D-Bus " << service_name << " ownership changed to "
              << new_owner;
    HandleMlDecisionServiceAvailableOrRestarted(true);
  }
}

}  // namespace policy
}  // namespace power_manager
