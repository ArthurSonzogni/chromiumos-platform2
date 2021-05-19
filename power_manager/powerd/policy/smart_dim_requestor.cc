// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/smart_dim_requestor.h"

#include <utility>

#include <chromeos/dbus/service_constants.h>

namespace power_manager {
namespace policy {
namespace {

// Timeout for RequestSmartDimDecision.
static constexpr base::TimeDelta kSmartDimDecisionTimeout =
    base::TimeDelta::FromSeconds(3);

}  // namespace

SmartDimRequestor::SmartDimRequestor() : weak_ptr_factory_(this) {}

void SmartDimRequestor::Init(
    system::DBusWrapperInterface* dbus_wrapper,
    base::RepeatingCallback<void()> defer_dim_callback) {
  defer_dim_callback_ = std::move(defer_dim_callback);
  dbus_wrapper_ = dbus_wrapper;
  ml_decision_dbus_proxy_ = dbus_wrapper->GetObjectProxy(
      chromeos::kMlDecisionServiceName, chromeos::kMlDecisionServicePath);
  dbus_wrapper->RegisterForServiceAvailability(
      ml_decision_dbus_proxy_,
      base::Bind(&SmartDimRequestor::HandleMlDecisionServiceAvailable,
                 weak_ptr_factory_.GetWeakPtr()));
}

void SmartDimRequestor::HandleMlDecisionServiceAvailable(bool available) {
  ml_decision_service_available_ = available;
  if (!available) {
    LOG(ERROR) << "Failed waiting for ml decision service to become "
                  "available";
    return;
  }
}

void SmartDimRequestor::RequestSmartDimDecision(base::TimeTicks now) {
  dbus::MethodCall method_call(
      chromeos::kMlDecisionServiceInterface,
      chromeos::kMlDecisionServiceShouldDeferScreenDimMethod);

  dbus_wrapper_->CallMethodAsync(
      ml_decision_dbus_proxy_, &method_call, kSmartDimDecisionTimeout,
      base::Bind(&SmartDimRequestor::HandleSmartDimResponse,
                 weak_ptr_factory_.GetWeakPtr()));

  waiting_for_smart_dim_decision_ = true;
  last_smart_dim_decision_request_time_ = now;
}

bool SmartDimRequestor::ReadyForRequest(base::TimeTicks now,
                                        base::TimeDelta screen_dim_imminent) {
  return request_smart_dim_decision_ && ml_decision_service_available_ &&
         !waiting_for_smart_dim_decision_ &&
         now - last_smart_dim_decision_request_time_ >= screen_dim_imminent;
}

void SmartDimRequestor::HandleSmartDimResponse(dbus::Response* response) {
  screen_dim_deferred_for_testing_ = false;
  if (!waiting_for_smart_dim_decision_) {
    LOG(WARNING) << "Smart dim decision is not being waited for";
    return;
  }

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

  screen_dim_deferred_for_testing_ = true;
  LOG(INFO) << "Smart dim decided to defer screen dimming";

  defer_dim_callback_.Run();
}

}  // namespace policy
}  // namespace power_manager
