// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ML_SMART_BATTERY_SMART_BATTERY_SERVICE_H_
#define ML_SMART_BATTERY_SMART_BATTERY_SERVICE_H_

#include <memory>
#include <string>
#include <vector>

#include "dbus_adaptors/org.chromium.MachineLearningSmartBattery.h"
#include "ml/smart_battery/tf_model_graph_executor.h"

namespace ml {

// Implementation of the smart battery dbus interface.
class SmartBatteryService
    : public org::chromium::MachineLearningSmartBatteryAdaptor,
      public org::chromium::MachineLearningSmartBatteryInterface {
 public:
  explicit SmartBatteryService(
      std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object);
  SmartBatteryService(const SmartBatteryService&) = delete;
  SmartBatteryService& operator=(const SmartBatteryService&) = delete;
  ~SmartBatteryService();

  // Register DBus object and interfaces.
  void RegisterAsync(
      const brillo::dbus_utils::AsyncEventSequencer::CompletionAction&
          completion_callback);

  // org::chromium::MachineLearningSmartBattery: (see
  // dbus_bindings/org.chromium.MachineLearningSmartBattery.xml).
  void RequestSmartBatteryDecision(
      std::unique_ptr<
          brillo::dbus_utils::DBusMethodResponse<bool, std::vector<double>>>
          response,
      const std::string& serialized_example_proto) override;

 private:
  const std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;
  std::unique_ptr<TfModelGraphExecutor> tf_model_graph_executor_;
};

}  // namespace ml

#endif  // ML_SMART_BATTERY_SMART_BATTERY_SERVICE_H_
