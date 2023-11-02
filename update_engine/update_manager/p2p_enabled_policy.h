//
// Copyright 2021 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_P2P_ENABLED_POLICY_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_P2P_ENABLED_POLICY_H_

#include <string>
#include <utility>

#include "update_engine/update_manager/policy_interface.h"

namespace chromeos_update_manager {

// Maximum number of times we'll allow using P2P for the same update payload.
extern const int kMaxP2PAttempts;
// Maximum period of time allowed for download a payload via P2P, in seconds.
extern const base::TimeDelta kMaxP2PAttemptsPeriod;

class P2PEnabledPolicyData : public PolicyDataInterface {
 public:
  P2PEnabledPolicyData() = default;
  virtual ~P2PEnabledPolicyData() = default;

  P2PEnabledPolicyData(const P2PEnabledPolicyData&) = delete;
  P2PEnabledPolicyData& operator=(const P2PEnabledPolicyData&) = delete;

  bool enabled() const { return enabled_; }
  void set_enabled(bool enabled) { enabled_ = enabled; }

  bool prev_enabled() const { return prev_enabled_; }
  void set_prev_enabled(bool prev_enabled) { prev_enabled_ = prev_enabled; }

 private:
  bool enabled_ = false;
  bool prev_enabled_ = false;
};

// Checks whether P2P is enabled. This may consult device policy and other
// global settings.
class P2PEnabledPolicy : public PolicyInterface {
 public:
  P2PEnabledPolicy() = default;
  virtual ~P2PEnabledPolicy() = default;

  P2PEnabledPolicy(const P2PEnabledPolicy&) = delete;
  P2PEnabledPolicy& operator=(const P2PEnabledPolicy&) = delete;

  EvalStatus Evaluate(EvaluationContext* ec,
                      State* state,
                      std::string* error,
                      PolicyDataInterface* data) const override;

  EvalStatus EvaluateDefault(EvaluationContext* ec,
                             State* state,
                             std::string* error,
                             PolicyDataInterface* data) const override;

 protected:
  std::string PolicyName() const override { return "P2PEnabledPolicy"; }
};

// Checks whether P2P is enabled, but blocks (returns
// |EvalStatus::kAskMeAgainLater|) until it is different from |prev_enabled|. If
// the P2P enabled status is not expected to change, will return immediately
// with |EvalStatus::kSucceeded|. This internally uses the |P2PEnabledPolicy|
// above.
class P2PEnabledChangedPolicy : public PolicyInterface {
 public:
  P2PEnabledChangedPolicy() = default;
  virtual ~P2PEnabledChangedPolicy() = default;

  P2PEnabledChangedPolicy(const P2PEnabledChangedPolicy&) = delete;
  P2PEnabledChangedPolicy& operator=(const P2PEnabledChangedPolicy&) = delete;

  EvalStatus Evaluate(EvaluationContext* ec,
                      State* state,
                      std::string* error,
                      PolicyDataInterface* data) const override;
  EvalStatus EvaluateDefault(EvaluationContext* ec,
                             State* state,
                             std::string* error,
                             PolicyDataInterface* data) const override;

 protected:
  std::string PolicyName() const override { return "P2PEnabledChangedPolicy"; }
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_P2P_ENABLED_POLICY_H_
