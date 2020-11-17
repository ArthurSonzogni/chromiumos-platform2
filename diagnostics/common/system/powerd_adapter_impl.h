// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_COMMON_SYSTEM_POWERD_ADAPTER_IMPL_H_
#define DIAGNOSTICS_COMMON_SYSTEM_POWERD_ADAPTER_IMPL_H_

#include <memory>

#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <base/observer_list.h>
#include <base/optional.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <power_manager/proto_bindings/power_supply_properties.pb.h>
#include <power_manager/proto_bindings/suspend.pb.h>

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
  void AddPowerObserver(PowerObserver* observer) override;
  void RemovePowerObserver(PowerObserver* observer) override;
  void AddLidObserver(LidObserver* observer) override;
  void RemoveLidObserver(LidObserver* observer) override;
  base::Optional<power_manager::PowerSupplyProperties>
  GetPowerSupplyProperties() override;

 private:
  // Handles PowerSupplyPoll signals emitted by powerd daemon.
  void HandlePowerSupplyPoll(dbus::Signal* signal);

  // Handles SuspendImminent signals emitted by powerd daemon.
  void HandleSuspendImminent(dbus::Signal* signal);

  // Handles DarkSuspendImminent signals emitted by powerd daemon.
  void HandleDarkSuspendImminent(dbus::Signal* signal);

  // Handles SuspendDone signals emitted by powerd daemon.
  void HandleSuspendDone(dbus::Signal* signal);

  // Handles LidClosed signals emitted by powerd daemon.
  void HandleLidClosed(dbus::Signal* signal);

  // Handles LidOpened signals emitted by powerd daemon.
  void HandleLidOpened(dbus::Signal* signal);

  base::ObserverList<PowerObserver> power_observers_;
  base::ObserverList<LidObserver> lid_observers_;

  // Owned by external D-Bus bus passed in constructor.
  dbus::ObjectProxy* bus_proxy_;

  base::WeakPtrFactory<PowerdAdapterImpl> weak_ptr_factory_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_COMMON_SYSTEM_POWERD_ADAPTER_IMPL_H_
