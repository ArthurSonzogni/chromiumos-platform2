// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/bluetooth/bluetooth_base_v2.h"

#include <utility>

#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/strings/string_number_conversions.h>
#include <base/types/expected.h>
#include <dbus/object_path.h>

#include "diagnostics/cros_healthd/system/floss_controller.h"
#include "diagnostics/cros_healthd/system/floss_event_hub.h"
#include "diagnostics/cros_healthd/utils/dbus_utils.h"
#include "diagnostics/dbus_bindings/bluetooth_manager/dbus-proxies.h"
#include "diagnostics/dbus_bindings/floss/dbus-proxies.h"

namespace diagnostics {
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

}  // namespace

BluetoothRoutineBaseV2::BluetoothRoutineBaseV2(Context* context)
    : context_(context) {
  CHECK(context_);
}

BluetoothRoutineBaseV2::~BluetoothRoutineBaseV2() = default;

void BluetoothRoutineBaseV2::Initialize(
    base::OnceCallback<void(bool)> on_finish) {
  manager_ = context_->floss_controller()->GetManager();
  if (!manager_) {
    LOG(ERROR) << "Failed to access Bluetooth manager proxy.";
    std::move(on_finish).Run(false);
    return;
  }

  event_subscriptions_.push_back(
      context_->floss_event_hub()->SubscribeManagerRemoved(
          base::BindRepeating(&BluetoothRoutineBaseV2::OnManagerRemoved,
                              weak_ptr_factory_.GetWeakPtr())));

  auto [on_success, on_error] = SplitDbusCallback(
      base::BindOnce(&BluetoothRoutineBaseV2::SetupDefaultAdapter,
                     weak_ptr_factory_.GetWeakPtr(), std::move(on_finish)));
  manager_->GetDefaultAdapterAsync(std::move(on_success), std::move(on_error));
}

void BluetoothRoutineBaseV2::SetupDefaultAdapter(
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
          base::BindRepeating(&BluetoothRoutineBaseV2::OnAdapterAdded,
                              weak_ptr_factory_.GetWeakPtr())));
  event_subscriptions_.push_back(
      context_->floss_event_hub()->SubscribeAdapterRemoved(
          base::BindRepeating(&BluetoothRoutineBaseV2::OnAdapterRemoved,
                              weak_ptr_factory_.GetWeakPtr())));

  if (!manager_) {
    LOG(ERROR) << "Failed to access Bluetooth manager proxy.";
    std::move(on_finish).Run(false);
    return;
  }

  // Setup initial powered state.
  auto [on_success, on_error] = SplitDbusCallback(
      base::BindOnce(&BluetoothRoutineBaseV2::CheckAdapterEnabledState,
                     weak_ptr_factory_.GetWeakPtr(), std::move(on_finish)));
  manager_->GetAdapterEnabledAsync(
      /*in_hci_interface=*/default_adapter_hci_, std::move(on_success),
      std::move(on_error));
}

void BluetoothRoutineBaseV2::CheckAdapterEnabledState(
    base::OnceCallback<void(bool)> on_finish,
    brillo::Error* error,
    bool powered) {
  if (error) {
    LOG(ERROR) << "Failed to get adapter powered state.";
    std::move(on_finish).Run(false);
    return;
  }

  initial_powered_state_ = powered;
  // Set up scoped closure runner for resetting adapter powered back to initial
  // powered state.
  reset_bluetooth_powered_ = base::ScopedClosureRunner(
      base::BindOnce(&ResetPoweredState, context_->floss_controller(), powered,
                     default_adapter_hci_));
  std::move(on_finish).Run(true);
}

org::chromium::bluetooth::BluetoothProxyInterface*
BluetoothRoutineBaseV2::GetDefaultAdapter() const {
  return default_adapter_;
}

bool BluetoothRoutineBaseV2::GetAdapterInitialPoweredState() const {
  CHECK(initial_powered_state_.has_value())
      << "GetAdapterInitialPoweredState should be called after routine is "
         "initialized successfully";
  return initial_powered_state_.value();
}

void BluetoothRoutineBaseV2::RunPreCheck(ResultCallback on_finish) {
  if (!manager_) {
    std::move(on_finish).Run(
        base::unexpected("Failed to access Bluetooth manager proxy."));
    return;
  }

  // The adapter must not be in discovery mode when powered is off.
  if (!GetAdapterInitialPoweredState()) {
    std::move(on_finish).Run(base::ok(true));
    return;
  }

  // The adapter must be existing when powered is on.
  auto adapter = GetDefaultAdapter();
  if (!adapter) {
    std::move(on_finish).Run(
        base::unexpected("Failed to get default adapter."));
    return;
  }

  auto [on_success, on_error] = SplitDbusCallback(
      base::BindOnce(&BluetoothRoutineBaseV2::HandleDiscoveringResponse,
                     weak_ptr_factory_.GetWeakPtr(), std::move(on_finish)));
  adapter->IsDiscoveringAsync(std::move(on_success), std::move(on_error));
}

void BluetoothRoutineBaseV2::HandleDiscoveringResponse(ResultCallback on_finish,
                                                       brillo::Error* error,
                                                       bool discovering) {
  if (error) {
    std::move(on_finish).Run(
        base::unexpected("Failed to get adapter discovering state."));
    return;
  }

  if (discovering) {
    // The pre-check is not passed when the default adapter is in discovery
    // mode. We should avoid running Bluetooth routines when the adapter is
    // actively scaninng or pairing.
    std::move(on_finish).Run(base::ok(false));
    return;
  }

  std::move(on_finish).Run(base::ok(true));
}

void BluetoothRoutineBaseV2::ChangeAdapterPoweredState(
    bool powered, ResultCallback on_finish) {
  if (!manager_) {
    std::move(on_finish).Run(
        base::unexpected("Failed to access Bluetooth manager proxy."));
    return;
  }

  auto [on_success, on_error] = SplitDbusCallback(base::BindOnce(
      &BluetoothRoutineBaseV2::HandleChangePoweredResponse,
      weak_ptr_factory_.GetWeakPtr(), powered, std::move(on_finish)));
  if (powered) {
    manager_->StartAsync(default_adapter_hci_, std::move(on_success),
                         std::move(on_error));
  } else {
    manager_->StopAsync(default_adapter_hci_, std::move(on_success),
                        std::move(on_error));
  }
}

void BluetoothRoutineBaseV2::HandleChangePoweredResponse(
    bool powered, ResultCallback on_finish, brillo::Error* error) {
  if (error) {
    // Changing powered errors are considered as failed status.
    std::move(on_finish).Run(base::ok(false));
    return;
  }

  // Ensure the adapter is added after powering on.
  if (powered && !default_adapter_) {
    LOG(INFO) << "Waiting for adapter added event";
    on_adapter_added_cbs_.push_back(base::BindOnce(std::move(on_finish), true));
    return;
  }

  std::move(on_finish).Run(base::ok(true));
}

void BluetoothRoutineBaseV2::OnAdapterAdded(
    org::chromium::bluetooth::BluetoothProxyInterface* adapter) {
  const auto adapter_path = GetAdapterPath(default_adapter_hci_);
  if (!adapter || adapter->GetObjectPath() != adapter_path) {
    return;
  }
  default_adapter_ = adapter;

  for (auto& cb : on_adapter_added_cbs_) {
    std::move(cb).Run();
  }
  on_adapter_added_cbs_.clear();
}

void BluetoothRoutineBaseV2::OnAdapterRemoved(
    const dbus::ObjectPath& adapter_path) {
  if (GetAdapterPath(default_adapter_hci_) == adapter_path)
    default_adapter_ = nullptr;
}

void BluetoothRoutineBaseV2::OnManagerRemoved(
    const dbus::ObjectPath& manager_path) {
  LOG(ERROR) << "Bluetooth manager proxy is removed unexpectedly";
  manager_ = nullptr;
}

}  // namespace diagnostics
