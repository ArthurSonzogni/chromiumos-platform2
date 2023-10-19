// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>

#include "federated/power_supply_training_condition.h"
#include "power_manager/proto_bindings/battery_saver.pb.h"
#include "power_manager/proto_bindings/power_supply_properties.pb.h"

namespace federated {
namespace {

// TODO(alanlxl): use 90% for now.
constexpr double kMinimumAdequateBatteryLevel = 90.0;

void OnSignalConnected(const std::string& interface_name,
                       const std::string& signal_name,
                       bool success) {
  if (!success) {
    LOG(ERROR) << "Failed to connect to signal " << interface_name << ":"
               << signal_name << ".";
  }
}

// Extracts battery saver state from dbus response or signal. Treats it as
// enabled if any errors to be conservative.
bool ExtractBatterySaverState(dbus::MessageReader& reader) {
  power_manager::BatterySaverModeState battery_saver_mode_state;
  if (!reader.PopArrayOfBytesAsProto(&battery_saver_mode_state)) {
    DVLOG(1) << "Failed to read BatterySaverModeState proto from dbus message.";
    return true;
  }

  if (!battery_saver_mode_state.has_enabled()) {
    DVLOG(1) << "BatterySaverModeState proto misses `enabled` field.";
    return true;
  }

  return battery_saver_mode_state.enabled();
}
}  // namespace

PowerSupplyTrainingCondition::PowerSupplyTrainingCondition(dbus::Bus* bus)
    : powerd_dbus_proxy_(bus->GetObjectProxy(
          power_manager::kPowerManagerServiceName,
          dbus::ObjectPath(power_manager::kPowerManagerServicePath))),
      enough_battery_(false),
      battery_saver_enabled_(true),
      weak_ptr_factory_(this) {
  DCHECK_NE(powerd_dbus_proxy_, nullptr);
  // Updates the battery status when receiving the kPowerSupplyPollSignal.
  // This signal is broadcasted periodically, so we don't need to fetch.
  powerd_dbus_proxy_->ConnectToSignal(
      power_manager::kPowerManagerInterface,
      power_manager::kPowerSupplyPollSignal,
      base::BindRepeating(&PowerSupplyTrainingCondition::OnPowerSupplyReceived,
                          weak_ptr_factory_.GetMutableWeakPtr()),
      base::BindOnce(&OnSignalConnected));

  // Battery saver mode
  powerd_dbus_proxy_->ConnectToSignal(
      power_manager::kPowerManagerInterface,
      power_manager::kBatterySaverModeStateChanged,
      base::BindRepeating(
          &PowerSupplyTrainingCondition::OnBatterySaverModeReceived,
          weak_ptr_factory_.GetMutableWeakPtr()),
      base::BindOnce(&OnSignalConnected));

  // Battery saver state signal is emitted only when the state changes. To get a
  // reliable initial status, we request it when power manager dbus service is
  // available.
  powerd_dbus_proxy_->WaitForServiceToBeAvailable(base::BindOnce(
      &PowerSupplyTrainingCondition::OnPowerManagerServiceAvailable,
      weak_ptr_factory_.GetMutableWeakPtr()));

  DVLOG(1) << "Construct PowerSupplyTrainingCondition";
}

bool PowerSupplyTrainingCondition::IsTrainingConditionSatisfied() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DVLOG(1) << "PowerSupplyTrainingCondition::IsTrainingConditionSatisfied: "
           << enough_battery_;
  return enough_battery_ && !battery_saver_enabled_;
}

void PowerSupplyTrainingCondition::OnPowerManagerServiceAvailable(
    bool service_available) {
  // Be conservative here.
  if (!service_available) {
    battery_saver_enabled_ = true;
    return;
  }

  dbus::MethodCall method_call(power_manager::kPowerManagerInterface,
                               power_manager::kGetBatterySaverModeState);

  std::unique_ptr<dbus::Response> dbus_response =
      powerd_dbus_proxy_
          ->CallMethodAndBlock(&method_call,
                               dbus::ObjectProxy::TIMEOUT_USE_DEFAULT)
          .value_or(nullptr);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to request battery saver mode state on "
                  "PowerSupplyTrainingCondition construction";
    return;
  }

  dbus::MessageReader reader(dbus_response.get());
  battery_saver_enabled_ = ExtractBatterySaverState(reader);
}

void PowerSupplyTrainingCondition::OnPowerSupplyReceived(
    dbus::Signal* const signal) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (signal == nullptr) {
    DVLOG(1) << "Received a null signal in OnPowerSupplyReceived.";
    enough_battery_ = false;
    return;
  }

  dbus::MessageReader reader(signal);
  power_manager::PowerSupplyProperties power_supply_proto;
  if (!reader.PopArrayOfBytesAsProto(&power_supply_proto)) {
    DVLOG(1) << "Failed to read PowerSupplyProperties proto from dbus message.";
    enough_battery_ = false;
    return;
  }

  // When battery is enough, or the device is plugged-in.
  enough_battery_ =
      (power_supply_proto.has_battery_percent() &&
       power_supply_proto.battery_percent() > kMinimumAdequateBatteryLevel) ||
      (power_supply_proto.has_battery_state() &&
       power_supply_proto.battery_state() !=
           power_manager::PowerSupplyProperties::DISCHARGING);
}

void PowerSupplyTrainingCondition::OnBatterySaverModeReceived(
    dbus::Signal* const signal) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // When errors happen, set `battery_saver_enabled_` true to be conservative.
  battery_saver_enabled_ = true;
  if (signal == nullptr) {
    DVLOG(1) << "Received a null signal in OnBatterySaverModeReceived.";
    return;
  }

  dbus::MessageReader reader(signal);
  battery_saver_enabled_ = ExtractBatterySaverState(reader);
}

}  // namespace federated
