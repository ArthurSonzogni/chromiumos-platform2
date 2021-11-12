// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_POLICY_DIM_ADVISOR_H_
#define POWER_MANAGER_POWERD_POLICY_DIM_ADVISOR_H_

#include <string>

#include <base/time/time.h>
#include <dbus/exported_object.h>
#include <dbus/message.h>

#include "power_manager/powerd/system/dbus_wrapper.h"

namespace power_manager {
namespace policy {

class StateController;

// DimAdvisor works as a advisor for the StateController to make dim decisions.
//   (1) It communicates with MLDecisionService to decide whether to defer a
//       screeen dimming.
//   (2) It listens to signals from HpsService to know the latest HpsResult.
class DimAdvisor : public system::DBusWrapperInterface::Observer {
 public:
  // Represents the signal result from Hps service.
  enum class HpsResult {
    UNKNOWN,
    POSITIVE,
    NEGATIVE,
  };

  DimAdvisor();
  ~DimAdvisor();

  // Initializes with `dbus_wrapper` and `state_controller`.
  void Init(system::DBusWrapperInterface* dbus_wrapper,
            StateController* state_controller);
  // Returns whether DimAdvisor is ready for making a smart dim query.
  bool ReadyForSmartDimRequest(base::TimeTicks now,
                               base::TimeDelta screen_dim_imminent_delay);
  // Calls MLService to decide whether to defer the dimming.
  void RequestSmartDimDecision(base::TimeTicks now);

  // Return whether the DimAdvisor is enabled.
  bool IsSmartDimEnabled();

  // Return whether the Hps service is enabled.
  bool IsHpsSenseEnabled();

  HpsResult GetHpsResult() { return hps_result_; }

  // DBusWrapperInterface::Observer:
  void OnDBusNameOwnerChanged(const std::string& service_name,
                              const std::string& old_owner,
                              const std::string& new_owner) override;

 private:
  // Handles the `ml_decision_dbus_proxy_` becoming initially available.
  void HandleMlDecisionServiceAvailableOrRestarted(bool available);
  // Handles smart dim response, serves as callback in RequestSmartDimDecision.
  void HandleSmartDimResponse(dbus::Response* response);
  // Handle Hps sense signal.
  void HandleHpsSenseSignal(dbus::Signal* signal);

  // True if hps service is connected.
  bool hps_sense_connected_ = false;
  // True if ml decision service is available.
  bool ml_decision_service_available_ = false;
  // True if there's a pending request waiting for response.
  bool waiting_for_smart_dim_decision_ = false;
  // Timestamp of the last smart dim decision requested. Used to prevent
  // consecutive requests with intervals shorter than screen_dim_imminent_delay,
  // see ReadyForSmartDimRequest.
  base::TimeTicks last_smart_dim_decision_request_time_;
  // HpsResult recorded.
  HpsResult hps_result_ = HpsResult::UNKNOWN;

  dbus::ObjectProxy* hps_dbus_proxy_ = nullptr;           // not owned
  dbus::ObjectProxy* ml_decision_dbus_proxy_ = nullptr;   // not owned
  system::DBusWrapperInterface* dbus_wrapper_ = nullptr;  // not owned
  StateController* state_controller_ = nullptr;           // not owned

  base::WeakPtrFactory<DimAdvisor> weak_ptr_factory_;
};

}  // namespace policy
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_POLICY_DIM_ADVISOR_H_
