// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_POLICY_SMART_DIM_REQUESTOR_H_
#define POWER_MANAGER_POWERD_POLICY_SMART_DIM_REQUESTOR_H_

#include <string>

#include <base/time/time.h>
#include <dbus/exported_object.h>
#include <dbus/message.h>

#include "power_manager/powerd/system/dbus_wrapper.h"

namespace power_manager {
namespace policy {

class StateController;

// SmartDimRequestor makes dbus calls to kMlDecisionServiceName to decide
// whether to defer the screen dimming.
class SmartDimRequestor : public system::DBusWrapperInterface::Observer {
 public:
  SmartDimRequestor();
  ~SmartDimRequestor();

  // Initializes with `dbus_wrapper` and `state_controller`.
  void Init(system::DBusWrapperInterface* dbus_wrapper,
            StateController* state_controller);
  // Returns whether SmartDimRequestor is ready for making a new query.
  bool ReadyForRequest(base::TimeTicks now,
                       base::TimeDelta screen_dim_imminent_delay);
  // Calls MLService to decide whether to defer the dimming.
  void RequestSmartDimDecision(base::TimeTicks now);

  // Return whether the SmartDimRequestor is enabled.
  bool IsEnabled();

  // DBusWrapperInterface::Observer:
  void OnDBusNameOwnerChanged(const std::string& service_name,
                              const std::string& old_owner,
                              const std::string& new_owner) override;

 private:
  // Handles the `ml_decision_dbus_proxy_` becoming initially available.
  void HandleMlDecisionServiceAvailableOrRestarted(bool available);
  // Handles smart dim response, serves as callback in RequestSmartDimDecision.
  void HandleSmartDimResponse(dbus::Response* response);

  // True if ml decision service is available.
  bool ml_decision_service_available_ = false;
  // True if there's a pending request waiting for response.
  bool waiting_for_smart_dim_decision_ = false;
  // Timestamp of the last smart dim decision requested. Used to prevent
  // consecutive requests with intervals shorter than screen_dim_imminent_delay,
  // see ReadyForRequest.
  base::TimeTicks last_smart_dim_decision_request_time_;

  dbus::ObjectProxy* ml_decision_dbus_proxy_ = nullptr;   // not owned
  system::DBusWrapperInterface* dbus_wrapper_ = nullptr;  // not owned
  StateController* state_controller_ = nullptr;           // not owned

  base::WeakPtrFactory<SmartDimRequestor> weak_ptr_factory_;
};

}  // namespace policy
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_POLICY_SMART_DIM_REQUESTOR_H_
