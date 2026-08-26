// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/thermal_event_handler.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/message.h>

#include "power_manager/common/clock.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/system/dbus_wrapper.h"
#include "power_manager/powerd/system/thermal/device_thermal_state.h"
#include "power_manager/powerd/system/thermal/thermal_device.h"
#include "power_manager/powerd/system/thermal/thermal_device_observer.h"
#include "power_manager/proto_bindings/thermal.pb.h"

namespace power_manager::policy {

ThermalEventHandler::ThermalEventHandler(
    std::vector<system::ThermalDeviceInterface*> thermal_devices,
    system::DBusWrapperInterface* dbus_wrapper)
    : dbus_wrapper_(dbus_wrapper),
      thermal_devices_(thermal_devices),
      clock_(std::make_unique<Clock>()),
      weak_ptr_factory_(this) {
  for (auto& device : thermal_devices) {
    DCHECK(device);
    device->AddObserver(this);
    if (device->GetType() == system::ThermalDeviceType::kSocCooling) {
      soc_devices_.push_back(device);
    } else {
      non_soc_devices_.push_back(device);
    }
  }
}

ThermalEventHandler::~ThermalEventHandler() {
  for (auto& device : thermal_devices_) {
    device->RemoveObserver(this);
  }
}

bool ThermalEventHandler::Init() {
  // Send current state to Chrome on Init.
  OnThermalChanged(nullptr);
  dbus_wrapper_->ExportMethod(
      kGetThermalStateMethod,
      base::BindRepeating(&ThermalEventHandler::OnGetThermalStateMethodCall,
                          weak_ptr_factory_.GetWeakPtr()));
  return true;
}

void ThermalEventHandler::OnGetThermalStateMethodCall(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  ThermalEvent protobuf;
  protobuf.set_thermal_state(DeviceThermalStateToProto(last_state_));
  protobuf.set_timestamp(
      (clock_->GetCurrentTime() - base::TimeTicks()).InMicroseconds());
  std::unique_ptr<dbus::Response> response =
      dbus::Response::FromMethodCall(method_call);
  dbus::MessageWriter writer(response.get());
  writer.AppendProtoAsArrayOfBytes(protobuf);
  std::move(response_sender).Run(std::move(response));
}

system::DeviceThermalState ThermalEventHandler::CalculateSocThermalState()
    const {
  if (soc_devices_.empty()) {
    return system::DeviceThermalState::kUnknown;
  }
  double soc_weighted_throttle = 0.0;
  double total_soc_weight = 0.0;
  for (const auto* dev : soc_devices_) {
    const double weight = dev->GetWeight();
    soc_weighted_throttle += weight * dev->GetThrottleRatio();
    total_soc_weight += weight;
  }
  if (total_soc_weight <= 0.0) {
    return system::DeviceThermalState::kUnknown;
  }
  double ratio = soc_weighted_throttle / total_soc_weight;
  if (std::isnan(ratio)) {
    return system::DeviceThermalState::kUnknown;
  }
  if (ratio >= system::kDefaultSocCoolingScale.critical) {
    return system::DeviceThermalState::kCritical;
  }
  if (ratio >= system::kDefaultSocCoolingScale.serious) {
    return system::DeviceThermalState::kSerious;
  }
  if (ratio >= system::kDefaultSocCoolingScale.fair) {
    return system::DeviceThermalState::kFair;
  }
  return system::DeviceThermalState::kNominal;
}

system::DeviceThermalState ThermalEventHandler::CalculateNonSocThermalState()
    const {
  system::DeviceThermalState state = system::DeviceThermalState::kUnknown;
  for (const auto* dev : non_soc_devices_) {
    auto dev_state = dev->GetThermalState();
    if (power_source_ == PowerSource::BATTERY &&
        dev->GetType() == system::ThermalDeviceType::kChargerCooling) {
      dev_state = system::DeviceThermalState::kUnknown;
    }
    state = std::max(state, dev_state);
  }
  return state;
}

void ThermalEventHandler::OnThermalChanged(
    system::ThermalDeviceInterface* device) {
  if (!device) {
    last_soc_state_ = CalculateSocThermalState();
    last_non_soc_state_ = CalculateNonSocThermalState();
  } else if (device->GetType() == system::ThermalDeviceType::kSocCooling) {
    const auto new_soc_state = CalculateSocThermalState();
    if (new_soc_state == last_soc_state_) {
      return;
    }
    last_soc_state_ = new_soc_state;
  } else {
    if (device->GetThermalState() == last_non_soc_state_) {
      return;
    }
    last_non_soc_state_ = CalculateNonSocThermalState();
  }

  const auto new_state = std::max(last_soc_state_, last_non_soc_state_);
  if (new_state == last_state_) {
    return;
  }

  ThermalEvent proto;
  proto.set_thermal_state(DeviceThermalStateToProto(new_state));
  proto.set_timestamp(
      (clock_->GetCurrentTime() - base::TimeTicks()).InMicroseconds());
  dbus_wrapper_->EmitSignalWithProtocolBuffer(kThermalEventSignal, proto);

  last_state_ = new_state;
}

void ThermalEventHandler::HandlePowerSourceChange(PowerSource source) {
  if (source == power_source_) {
    return;
  }

  power_source_ = source;
  OnThermalChanged(nullptr);
}

}  // namespace power_manager::policy
