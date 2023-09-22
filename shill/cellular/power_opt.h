// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CELLULAR_POWER_OPT_H_
#define SHILL_CELLULAR_POWER_OPT_H_

#include <string>
#include <unordered_map>

#include "shill/cellular/cellular.h"
#include "shill/cellular/cellular_consts.h"
#include "shill/manager.h"
#include "shill/metrics.h"
#include "shill/service.h"

namespace shill {

class PowerOpt {
 public:
  enum class PowerState {
    kUnknown,
    kOn,
    kLow,
    kOff,
  };

  enum class PowerOptReason {
    kNoServiceGeneral,
    kNoServiceInvalidApn,
    kNoServiceNoSubscription,
    kNoServiceAdminRestriction,
  };

  explicit PowerOpt(Manager* manager);
  void NotifyConnectionFailInvalidApn(const std::string& iccid);
  void NotifyRegistrationSuccess(const std::string& iccid);
  void UpdateDurationSinceLastOnline(const base::Time& last_online,
                                     bool is_user_request);
  bool UpdatePowerState(const std::string& iccid, PowerState state);
  bool AddOptInfoForNewService(const std::string& iccid);
  base::TimeDelta GetTimeSinceLastOnline(const std::string& iccid);
  base::TimeDelta GetInvalidApnDuration(const std::string& iccid);
  PowerState GetPowerState(const std::string& iccid);

 private:
  FRIEND_TEST(PowerOptTest, LowPowerLongNotOnline);
  FRIEND_TEST(PowerOptTest, LowPowerInvalidApn);

  enum class PowerEvent {
    kUnkown,
    kInvalidApn,
    kNoRoamingAgreement,
    // Blocked by admin etc
    kServiceBlocked,
    kLongNotOnline,
  };

  // time threshold since power on to set modem low power due to invalid Apn
  // failures
  static constexpr base::TimeDelta kNoServiceInvalidApnTimeThreshold =
      base::Hours(24);
  static constexpr base::TimeDelta kLastOnlineShortThreshold = base::Days(5);
  static constexpr base::TimeDelta kLastOnlineLongThreshold = base::Days(30);
  // if |time_since_last_online| is over threshold, but user has init a connect
  // request, reset |time_since_last_online| to |kUserRequestGracePeriod|
  // less than the threshold to allow user retry.
  static constexpr base::TimeDelta kUserRequestGracePeriod = base::Hours(1);

  struct PowerOptimizationInfo {
    base::Time last_connect_fail_invalid_apn_time;
    base::TimeDelta no_service_invalid_apn_duration;
    base::TimeDelta time_since_last_online;
    PowerState power_state;
  };

  Manager* manager_;
  std::unordered_map<std::string, PowerOptimizationInfo> opt_info_;
  PowerOptimizationInfo* current_opt_info_;

  PowerOpt::PowerState PerformPowerOptimization(PowerEvent event);
  bool RequestPowerStateChange(PowerOpt::PowerState);
};
}  // namespace shill

#endif  // SHILL_CELLULAR_POWER_OPT_H_
