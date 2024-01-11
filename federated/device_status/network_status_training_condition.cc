// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/logging.h>
#include <brillo/errors/error.h>
#include <brillo/variant_dictionary.h>
#include <dbus/bus.h>
#include <shill/dbus-constants.h>
#include <shill/dbus/client/client.h>

#include "federated/device_status/network_status_training_condition.h"
#include "federated/metrics.h"

namespace federated {
namespace {

using org::chromium::flimflam::ManagerProxyInterface;

void OnSignalConnected(const std::string& interface,
                       const std::string& signal,
                       bool successful) {
  if (!successful) {
    LOG(ERROR) << "Could not connect to signal " << signal << " on interface "
               << interface;
  }
}
}  // namespace

NetworkStatusTrainingCondition::NetworkStatusTrainingCondition(
    ShillProxyInterface* shill_proxy)
    : shill_proxy_(shill_proxy),
      shill_default_service_path_("uninitialized"),
      is_metered_(false) {
  // Subscribes to the shill manager's PropertyChanged signal.
  shill_proxy_->GetShillManagerProxy()->RegisterPropertyChangedSignalHandler(
      base::BindRepeating(
          &NetworkStatusTrainingCondition::OnShillManagerPropertyChanged,
          base::Unretained(this)),
      base::BindOnce(&OnSignalConnected));

  // Attempts to read the initial connection status.
  brillo::VariantDictionary properties;
  brillo::ErrorPtr error;

  if (shill_proxy_->GetShillManagerProxy()->GetProperties(&properties,
                                                          &error)) {
    const auto& shill_default_service =
        properties.find(shill::kDefaultServiceProperty);
    if (shill_default_service != properties.end()) {
      OnShillManagerPropertyChanged(shill_default_service->first,
                                    shill_default_service->second);
    }
  }

  DVLOG(1) << "Construct NetworkStatusTrainingCondition";
}

bool NetworkStatusTrainingCondition::IsTrainingConditionSatisfiedToStart()
    const {
  if (is_metered_) {
    Metrics::GetInstance()->LogTrainingConditionToStartResult(
        TrainingConditionResult::kMeteredNetwork);
  }

  return !is_metered_;
}

bool NetworkStatusTrainingCondition::IsTrainingConditionSatisfiedToContinue()
    const {
  if (is_metered_) {
    Metrics::GetInstance()->LogTrainingConditionToContinueResult(
        TrainingConditionResult::kMeteredNetwork);
  }

  return !is_metered_;
}

void NetworkStatusTrainingCondition::OnShillManagerPropertyChanged(
    const std::string& name, const brillo::Any& value) {
  if (name == shill::kDefaultServiceProperty) {
    dbus::ObjectPath service_path = value.TryGet<dbus::ObjectPath>();
    if (!service_path.IsValid()) {
      LOG(WARNING) << "Got an invalid DefaultService path. The property value "
                      "contains a "
                   << value.GetUndecoratedTypeName()
                   << ", read as the object path: '" << service_path.value()
                   << "'";
    }
    ProcessShillDefaultService(service_path);
    DVLOG(1) << "After ProcessDefaultShillService, is_metered_ = "
             << is_metered_;
  }
}

void NetworkStatusTrainingCondition::ProcessShillDefaultService(
    const dbus::ObjectPath& service_path) {
  // Unchanged shill default service_path implies unchanged connection type.
  if (shill_default_service_path_ == service_path) {
    return;
  }

  shill_default_service_path_ = service_path;

  // Invalid service_path implies invalid connection status, we treat it as not
  // metered.
  if (!shill_default_service_path_.IsValid() ||
      shill_default_service_path_.value() == "/") {
    is_metered_ = false;
    return;
  }

  // Creates a disposable shill service proxy to request the current connection
  // properties.
  auto shill_service_proxy =
      shill_proxy_->GetShillServiceProxyForPath(shill_default_service_path_);

  // Get the connection properties synchronously.
  brillo::VariantDictionary service_properties;
  brillo::ErrorPtr error;
  if (!shill_service_proxy->GetProperties(&service_properties, &error)) {
    is_metered_ = false;
    return;
  }

  // Get the connection metered property.
  if (service_properties.find(shill::kMeteredProperty) ==
      service_properties.end()) {
    is_metered_ = false;
  } else {
    is_metered_ = brillo::GetVariantValueOrDefault<bool>(
        service_properties, shill::kMeteredProperty);
  }
}

}  // namespace federated
