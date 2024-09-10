// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/bluetooth/floss/bluetooth_base.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include <base/cancelable_callback.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/strings/string_number_conversions.h>
#include <base/task/single_thread_task_runner.h>
#include <base/types/expected.h>
#include <dbus/object_path.h>

#include "diagnostics/cros_healthd/routines/bluetooth/bluetooth_constants.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/system/floss_controller.h"
#include "diagnostics/cros_healthd/system/floss_event_hub.h"
#include "diagnostics/cros_healthd/utils/dbus_utils.h"
#include "diagnostics/dbus_bindings/bluetooth_manager/dbus-proxies.h"
#include "diagnostics/dbus_bindings/floss/dbus-proxies.h"

namespace diagnostics::floss {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

dbus::ObjectPath GetAdapterPath(int32_t hci_interface) {
  return dbus::ObjectPath("/org/chromium/bluetooth/hci" +
                          base::NumberToString(hci_interface) + "/adapter");
}

void ResetPoweredState(FlossController* floss_controller,
                       bool initial_powered_state,
                       int32_t hci_interface) {
  auto manager = floss_controller->GetManager();
  if (!manager) {
    LOG(ERROR) << "Failed to access Bluetooth manager proxy when resetting.";
    return;
  }
  if (initial_powered_state) {
    manager->StartAsync(hci_interface, base::DoNothing(), base::DoNothing());
  } else {
    manager->StopAsync(hci_interface, base::DoNothing(), base::DoNothing());
  }
}

void CancelAdapterDiscovery(FlossController* floss_controller,
                            int32_t hci_interface) {
  auto adapter_path = GetAdapterPath(hci_interface);
  for (const auto& adapter : floss_controller->GetAdapters()) {
    if (adapter && adapter->GetObjectPath() == adapter_path)
      adapter->CancelDiscoveryAsync(base::DoNothing(), base::DoNothing());
  }
}

}  // namespace

BluetoothRoutineBase::BluetoothRoutineBase(Context* context)
    : context_(context) {
  CHECK(context_);
}

BluetoothRoutineBase::~BluetoothRoutineBase() = default;

void BluetoothRoutineBase::Initialize(
    base::OnceCallback<void(bool)> on_finish) {
  manager_ = context_->floss_controller()->GetManager();
  if (!manager_) {
    LOG(ERROR) << "Failed to access Bluetooth manager proxy.";
    std::move(on_finish).Run(false);
    return;
  }

  event_subscriptions_.push_back(
      context_->floss_event_hub()->SubscribeManagerRemoved(
          base::BindRepeating(&BluetoothRoutineBase::OnManagerRemoved,
                              weak_ptr_factory_.GetWeakPtr())));

  auto [on_success, on_error] = SplitDbusCallback(
      base::BindOnce(&BluetoothRoutineBase::CheckFlossEnabledState,
                     weak_ptr_factory_.GetWeakPtr(), std::move(on_finish)));
  manager_->GetFlossEnabledAsync(std::move(on_success), std::move(on_error));
}

void BluetoothRoutineBase::CheckFlossEnabledState(
    base::OnceCallback<void(bool)> on_finish,
    brillo::Error* error,
    bool floss_enabled) {
  if (error || !floss_enabled) {
    LOG(ERROR) << "Failed to ensure that floss is enabled.";
    std::move(on_finish).Run(false);
    return;
  }

  auto [on_success, on_error] = SplitDbusCallback(
      base::BindOnce(&BluetoothRoutineBase::SetupDefaultAdapter,
                     weak_ptr_factory_.GetWeakPtr(), std::move(on_finish)));
  manager_->GetDefaultAdapterAsync(std::move(on_success), std::move(on_error));
}

void BluetoothRoutineBase::SetupDefaultAdapter(
    base::OnceCallback<void(bool)> on_finish,
    brillo::Error* error,
    int32_t hci_interface) {
  if (error) {
    LOG(ERROR) << "Failed to get default Bluetooth adapter.";
    std::move(on_finish).Run(false);
    return;
  }
  default_adapter_hci_ = hci_interface;

  // Setup default adapter.
  const auto adapter_path = GetAdapterPath(default_adapter_hci_);
  for (const auto& adapter : context_->floss_controller()->GetAdapters()) {
    if (adapter && adapter->GetObjectPath() == adapter_path)
      default_adapter_ = adapter;
  }

  event_subscriptions_.push_back(
      context_->floss_event_hub()->SubscribeAdapterAdded(
          base::BindRepeating(&BluetoothRoutineBase::OnAdapterAdded,
                              weak_ptr_factory_.GetWeakPtr())));
  event_subscriptions_.push_back(
      context_->floss_event_hub()->SubscribeAdapterRemoved(
          base::BindRepeating(&BluetoothRoutineBase::OnAdapterRemoved,
                              weak_ptr_factory_.GetWeakPtr())));
  event_subscriptions_.push_back(
      context_->floss_event_hub()->SubscribeAdapterPoweredChanged(
          base::BindRepeating(&BluetoothRoutineBase::OnAdapterPoweredChanged,
                              weak_ptr_factory_.GetWeakPtr())));

  if (!manager_) {
    LOG(ERROR) << "Failed to access Bluetooth manager proxy.";
    std::move(on_finish).Run(false);
    return;
  }

  // Setup initial powered state.
  auto [on_success, on_error] = SplitDbusCallback(
      base::BindOnce(&BluetoothRoutineBase::CheckAdapterEnabledState,
                     weak_ptr_factory_.GetWeakPtr(), std::move(on_finish)));
  manager_->GetAdapterEnabledAsync(
      /*in_hci_interface=*/default_adapter_hci_, std::move(on_success),
      std::move(on_error));
}

void BluetoothRoutineBase::CheckAdapterEnabledState(
    base::OnceCallback<void(bool)> on_finish,
    brillo::Error* error,
    bool powered) {
  if (error) {
    LOG(ERROR) << "Failed to get adapter powered state.";
    std::move(on_finish).Run(false);
    return;
  }

  initial_powered_state_ = powered;
  current_powered_ = powered;
  // Set up scoped closure runner for resetting adapter powered back to initial
  // powered state.
  reset_bluetooth_powered_ = base::ScopedClosureRunner(
      base::BindOnce(&ResetPoweredState, context_->floss_controller(), powered,
                     default_adapter_hci_));
  std::move(on_finish).Run(true);
}

org::chromium::bluetooth::BluetoothProxyInterface*
BluetoothRoutineBase::GetDefaultAdapter() const {
  return default_adapter_;
}

bool BluetoothRoutineBase::GetAdapterInitialPoweredState() const {
  CHECK(initial_powered_state_.has_value())
      << "GetAdapterInitialPoweredState should be called after routine is "
         "initialized successfully";
  return initial_powered_state_.value();
}

void BluetoothRoutineBase::RunPreCheck(
    base::OnceCallback<void(std::optional<std::string>)> on_finish) {
  if (!manager_) {
    std::move(on_finish).Run("Failed to access Bluetooth manager proxy.");
    return;
  }

  // The adapter must not be in discovery mode when powered is off.
  if (!GetAdapterInitialPoweredState()) {
    std::move(on_finish).Run(std::nullopt);
    return;
  }

  // The default adapter must be existing when powered is on.
  if (!default_adapter_) {
    std::move(on_finish).Run("Failed to get default adapter.");
    return;
  }

  auto [on_success, on_error] = SplitDbusCallback(
      base::BindOnce(&BluetoothRoutineBase::HandleDiscoveringResponse,
                     weak_ptr_factory_.GetWeakPtr(), std::move(on_finish)));
  default_adapter_->IsDiscoveringAsync(std::move(on_success),
                                       std::move(on_error));
}

void BluetoothRoutineBase::HandleDiscoveringResponse(
    base::OnceCallback<void(std::optional<std::string>)> on_finish,
    brillo::Error* error,
    bool discovering) {
  if (error) {
    std::move(on_finish).Run("Failed to get adapter discovering state.");
    return;
  }

  if (discovering) {
    // The pre-check is not passed when the default adapter is in discovery
    // mode. We should avoid running Bluetooth routines when the adapter is
    // actively scaninng or pairing.
    std::move(on_finish).Run(kBluetoothRoutineFailedDiscoveryMode);
    return;
  }

  std::move(on_finish).Run(std::nullopt);
}

void BluetoothRoutineBase::SetAdapterPoweredState(bool powered,
                                                  ResultCallback on_finish) {
  if (!manager_) {
    LOG(ERROR) << "Failed to access Bluetooth manager proxy.";
    std::move(on_finish).Run(std::nullopt);
    return;
  }

  if (powered == current_powered_) {
    std::move(on_finish).Run(current_powered_);
    return;
  }

  auto [on_success, on_error] = SplitDbusCallback(base::BindOnce(
      &BluetoothRoutineBase::HandleSetPoweredResponse,
      weak_ptr_factory_.GetWeakPtr(), powered, std::move(on_finish)));
  if (powered) {
    manager_->StartAsync(default_adapter_hci_, std::move(on_success),
                         std::move(on_error));
  } else {
    manager_->StopAsync(default_adapter_hci_, std::move(on_success),
                        std::move(on_error));
  }
}

void BluetoothRoutineBase::HandleSetPoweredResponse(bool powered,
                                                    ResultCallback on_finish,
                                                    brillo::Error* error) {
  if (error) {
    std::move(on_finish).Run(std::nullopt);
    return;
  }

  // The successful D-Bus call doesn't mean that the enabling or disabling is
  // successful. We should check the powered changed events.
  LOG(INFO) << "Waiting for adapter powered changed event.";
  on_adapter_powered_changed_cb_ = std::move(on_finish);
  timeout_cb_.Reset(
      base::BindOnce(&BluetoothRoutineBase::OnAdapterEnabledEventTimeout,
                     weak_ptr_factory_.GetWeakPtr()));
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, timeout_cb_.callback(), kAdapterPoweredChangedTimeout);
}

void BluetoothRoutineBase::OnAdapterEnabledEventTimeout() {
  if (!on_adapter_powered_changed_cb_.is_null()) {
    std::move(on_adapter_powered_changed_cb_).Run(current_powered_);
  }
}

void BluetoothRoutineBase::OnAdapterAdded(
    org::chromium::bluetooth::BluetoothProxyInterface* adapter) {
  const auto adapter_path = GetAdapterPath(default_adapter_hci_);
  if (!adapter || adapter->GetObjectPath() != adapter_path) {
    return;
  }
  default_adapter_ = adapter;
}

void BluetoothRoutineBase::OnAdapterRemoved(
    const dbus::ObjectPath& adapter_path) {
  if (GetAdapterPath(default_adapter_hci_) == adapter_path)
    default_adapter_ = nullptr;
}

void BluetoothRoutineBase::OnAdapterPoweredChanged(int32_t hci_interface,
                                                   bool powered) {
  if (hci_interface != default_adapter_hci_) {
    return;
  }
  current_powered_ = powered;

  // Bluetooth routines should be able to access adapter instance directly after
  // powering on successfully. Add a safeguard to ensure that `default_adapter_`
  // is not null . If so, report null as error.
  std::optional<bool> got_powered = powered;
  if (powered && default_adapter_ == nullptr) {
    LOG(ERROR) << "Failed to get non-null default adapter after powering on";
    got_powered = std::nullopt;
  }

  timeout_cb_.Cancel();
  if (!on_adapter_powered_changed_cb_.is_null()) {
    std::move(on_adapter_powered_changed_cb_).Run(got_powered);
  }
}

void BluetoothRoutineBase::OnManagerRemoved(
    const dbus::ObjectPath& manager_path) {
  LOG(ERROR) << "Bluetooth manager proxy is removed unexpectedly";
  manager_ = nullptr;
}

void BluetoothRoutineBase::SetupStopDiscoveryJob() {
  adapter_stop_discovery_ = base::ScopedClosureRunner(
      base::BindOnce(&CancelAdapterDiscovery, context_->floss_controller(),
                     default_adapter_hci_));
}

}  // namespace diagnostics::floss
