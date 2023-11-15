// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <brillo/variant_dictionary.h>
#include <dbus/bus.h>
#include <shill/dbus/client/client.h>
#include <memory>
#include <utility>

#include "federated/metrics.h"
#include "federated/network_status_training_condition.h"

namespace federated {

NetworkStatusTrainingCondition::NetworkStatusTrainingCondition(
    std::unique_ptr<shill::Client> network_client)
    : dbus_network_client_(std::move(network_client)) {
  DVLOG(1) << "Construct NetworkStatusTrainingCondition";
}

bool NetworkStatusTrainingCondition::IsTrainingConditionSatisfiedToStart()
    const {
  bool satisfied = !IsNetworkMetered();
  if (!satisfied) {
    Metrics::GetInstance()->LogTrainingConditionToStartResult(
        TrainingConditionResult::kMeteredNetwork);
  }

  return satisfied;
}

bool NetworkStatusTrainingCondition::IsTrainingConditionSatisfiedToContinue()
    const {
  bool satisfied = !IsNetworkMetered();
  if (!satisfied) {
    Metrics::GetInstance()->LogTrainingConditionToContinueResult(
        TrainingConditionResult::kMeteredNetwork);
  }

  return satisfied;
}

// Check whether the network metered or not
bool NetworkStatusTrainingCondition::IsNetworkMetered() const {
  auto service_properties = dbus_network_client_->GetDefaultServiceProperties();
  // Let's be conservative and treat unexpected result as metered network.
  if (service_properties == nullptr ||
      service_properties->find(shill::kMeteredProperty) ==
          service_properties->end()) {
    // TODO(b/229921446): Make a new metric
    return true;
  }

  auto is_metered = brillo::GetVariantValueOrDefault<bool>(
      *service_properties, shill::kMeteredProperty);

  return is_metered;
}

}  // namespace federated
