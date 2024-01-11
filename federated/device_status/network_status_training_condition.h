// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_DEVICE_STATUS_NETWORK_STATUS_TRAINING_CONDITION_H_
#define FEDERATED_DEVICE_STATUS_NETWORK_STATUS_TRAINING_CONDITION_H_

#include <memory>
#include <string>

#include <brillo/any.h>
#include <dbus/object_path.h>

#include "federated/device_status/shill_proxy_interface.h"
#include "federated/device_status/training_condition.h"

namespace dbus {
class Bus;
}  // namespace dbus

namespace federated {

// Monitors the network status and answers whether there the conditions
// are satisfied. Currently, we check that the network is not metered
class NetworkStatusTrainingCondition : public TrainingCondition {
 public:
  explicit NetworkStatusTrainingCondition(ShillProxyInterface* shill_proxy);

  NetworkStatusTrainingCondition(const NetworkStatusTrainingCondition&) =
      delete;
  NetworkStatusTrainingCondition& operator=(
      const NetworkStatusTrainingCondition&) = delete;
  ~NetworkStatusTrainingCondition() override = default;

  // TrainingCondition:
  [[nodiscard]] bool IsTrainingConditionSatisfiedToStart() const override;
  [[nodiscard]] bool IsTrainingConditionSatisfiedToContinue() const override;

 private:
  void OnShillManagerPropertyChanged(const std::string& name,
                                     const brillo::Any& value);

  void ProcessShillDefaultService(const dbus::ObjectPath& service_path);

  // Handles dbus proxies to shill daemon.
  std::unique_ptr<ShillProxyInterface> shill_proxy_;
  dbus::ObjectPath shill_default_service_path_;
  // This is thread-safe.
  std::atomic_bool is_metered_;
};

}  // namespace federated

#endif  // FEDERATED_DEVICE_STATUS_NETWORK_STATUS_TRAINING_CONDITION_H_
