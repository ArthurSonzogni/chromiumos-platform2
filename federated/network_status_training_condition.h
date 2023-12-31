// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_NETWORK_STATUS_TRAINING_CONDITION_H_
#define FEDERATED_NETWORK_STATUS_TRAINING_CONDITION_H_

#include <memory>

#include <base/memory/ref_counted.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST
#include <shill/dbus/client/client.h>

#include "federated/training_condition.h"

namespace dbus {
class Bus;
}  // namespace dbus

namespace federated {

// Monitors the network status and answers whether there the conditions
// are satisfied. Currently, we check that the network is not metered
class NetworkStatusTrainingCondition : public TrainingCondition {
 public:
  explicit NetworkStatusTrainingCondition(
      std::unique_ptr<shill::Client> network_client);
  NetworkStatusTrainingCondition(const NetworkStatusTrainingCondition&) =
      delete;
  NetworkStatusTrainingCondition& operator=(
      const NetworkStatusTrainingCondition&) = delete;
  ~NetworkStatusTrainingCondition() override = default;

  // TrainingCondition:
  [[nodiscard]] bool IsTrainingConditionSatisfiedToStart() const override;
  [[nodiscard]] bool IsTrainingConditionSatisfiedToContinue() const override;

 private:
  // friend class NetworkStatusTrainingConditionTest;
  FRIEND_TEST(NetworkStatusTrainingConditionTest, IsNetworkMetered);

  bool IsNetworkMetered() const;
  const std::unique_ptr<shill::Client> dbus_network_client_;
};

}  // namespace federated

#endif  // FEDERATED_NETWORK_STATUS_TRAINING_CONDITION_H_
