// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_COMMON_SYSTEM_POWERD_ADAPTER_IMPL_H_
#define DIAGNOSTICS_COMMON_SYSTEM_POWERD_ADAPTER_IMPL_H_

#include <memory>
#include <optional>

#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <dbus/bus.h>
#include <power_manager/proto_bindings/power_supply_properties.pb.h>

#include "diagnostics/common/system/powerd_adapter.h"

namespace diagnostics {

// PowerdAdapter interface implementation that observes D-Bus signals from
// powerd daemon.
class PowerdAdapterImpl : public PowerdAdapter {
 public:
  explicit PowerdAdapterImpl(const scoped_refptr<dbus::Bus>& bus);
  PowerdAdapterImpl(const PowerdAdapterImpl&) = delete;
  PowerdAdapterImpl& operator=(const PowerdAdapterImpl&) = delete;
  ~PowerdAdapterImpl() override;

  // PowerdAdapter overrides:
  std::optional<power_manager::PowerSupplyProperties> GetPowerSupplyProperties()
      override;

 private:
  // Owned by external D-Bus bus passed in constructor.
  dbus::ObjectProxy* bus_proxy_;

  base::WeakPtrFactory<PowerdAdapterImpl> weak_ptr_factory_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_COMMON_SYSTEM_POWERD_ADAPTER_IMPL_H_
