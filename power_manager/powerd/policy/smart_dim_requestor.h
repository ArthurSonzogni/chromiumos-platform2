// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_POLICY_SMART_DIM_REQUESTOR_H_
#define POWER_MANAGER_POWERD_POLICY_SMART_DIM_REQUESTOR_H_

#include <base/time/time.h>
#include <dbus/exported_object.h>
#include <dbus/message.h>

#include "power_manager/powerd/system/dbus_wrapper.h"

namespace power_manager {
namespace policy {

// SmartDimRequestor works as a helper class that
// (1) exposes ReadyForRequest and RequestSmartDimDecision for the
//     StateController to call to make a smart dim request.
// (2) makes dbus calls to kMlDecisionServiceName to decide whether to defer the
//     screen dimming.
class SmartDimRequestor {
 public:
  SmartDimRequestor();
  // Initialize `ml_decision_dbus_proxy_` with `dbus_wrapper`.
  void Init(system::DBusWrapperInterface* dbus_wrapper,
            base::RepeatingCallback<void()> defer_dim_callback);
  // Returns whether SmartDimRequestor is ready for making a new query.
  bool ReadyForRequest(base::TimeTicks now,
                       base::TimeDelta screen_dim_imminent);
  // Calls MLService to decide whether to defer the dimming.
  void RequestSmartDimDecision(base::TimeTicks now);

  bool request_smart_dim_decision() { return request_smart_dim_decision_; }

  void set_request_smart_dim_decision_for_testing(bool should_ask) {
    request_smart_dim_decision_ = should_ask;
  }

  bool screen_dim_deferred_for_testing() const {
    return screen_dim_deferred_for_testing_;
  }

 private:
  // Handles the `ml_decision_dbus_proxy_` becoming initially available.
  void HandleMlDecisionServiceAvailable(bool available);
  // Handles smart dim response, serves as callback in RequestSmartDimDecision.
  void HandleSmartDimResponse(dbus::Response* response);

  // Should powerd request smart dim decision via D-Bus service? May be disabled
  // by tests.
  bool request_smart_dim_decision_ = true;
  // True if ml decision service is available.
  bool ml_decision_service_available_ = false;
  // True if there's a pending request waiting for response.
  bool waiting_for_smart_dim_decision_ = false;
  // True if the most recent RequestSmartDimDecision call returned true.
  // Used by unit tests.
  bool screen_dim_deferred_for_testing_ = false;
  // Time of the last request of RequestSmartDimDecision. Used to prevent
  // consecutive requests with intervals shorter than screen_dim_imminent_delay,
  // see ReadyForRequest
  base::TimeTicks last_smart_dim_decision_request_time_;

  dbus::ObjectProxy* ml_decision_dbus_proxy_ = nullptr;   // not owned
  system::DBusWrapperInterface* dbus_wrapper_ = nullptr;  // not owned

  // Called when smart dim wants to defer ths screen dim.
  // This is passed in from StateController.
  base::RepeatingCallback<void()> defer_dim_callback_;

  base::WeakPtrFactory<SmartDimRequestor> weak_ptr_factory_;
};

}  // namespace policy
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_POLICY_SMART_DIM_REQUESTOR_H_
