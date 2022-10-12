// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/common/system/powerd_adapter_impl.h"

#include <optional>
#include <string>

#include <base/check.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/time/time.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>
#include <dbus/power_manager/dbus-constants.h>

namespace diagnostics {

namespace {

// The maximum amount of time to wait for a powerd response.
constexpr base::TimeDelta kPowerManagerDBusTimeout = base::Seconds(3);

}  // namespace

PowerdAdapterImpl::PowerdAdapterImpl(const scoped_refptr<dbus::Bus>& bus)
    : bus_proxy_(bus->GetObjectProxy(
          power_manager::kPowerManagerServiceName,
          dbus::ObjectPath(power_manager::kPowerManagerServicePath))),
      weak_ptr_factory_(this) {
  DCHECK(bus);
  DCHECK(bus_proxy_);
}

PowerdAdapterImpl::~PowerdAdapterImpl() = default;

std::optional<power_manager::PowerSupplyProperties>
PowerdAdapterImpl::GetPowerSupplyProperties() {
  dbus::MethodCall method_call(power_manager::kPowerManagerInterface,
                               power_manager::kGetPowerSupplyPropertiesMethod);
  auto response = bus_proxy_->CallMethodAndBlock(
      &method_call, kPowerManagerDBusTimeout.InMilliseconds());

  if (!response) {
    LOG(ERROR) << "Failed to call powerd D-Bus method: "
               << power_manager::kGetPowerSupplyPropertiesMethod;
    return std::nullopt;
  }

  dbus::MessageReader reader(response.get());
  power_manager::PowerSupplyProperties power_supply_proto;
  if (!reader.PopArrayOfBytesAsProto(&power_supply_proto)) {
    LOG(ERROR) << "Could not successfully read PowerSupplyProperties protobuf";
    return std::nullopt;
  }

  return power_supply_proto;
}

}  // namespace diagnostics
