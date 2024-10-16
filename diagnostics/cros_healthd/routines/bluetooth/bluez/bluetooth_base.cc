// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/bluetooth/bluez/bluetooth_base.h"

#include <cstdint>
#include <string>
#include <utility>

#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>

#include "diagnostics/cros_healthd/routines/bluetooth/bluetooth_constants.h"
#include "diagnostics/cros_healthd/system/bluez_controller.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/system/floss_controller.h"
#include "diagnostics/cros_healthd/utils/dbus_utils.h"
#include "diagnostics/dbus_bindings/bluetooth_manager/dbus-proxies.h"

namespace diagnostics::bluez {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

void ResetPoweredState(BluezController* bluez_controller,
                       bool initial_powered) {
  auto adapters = bluez_controller->GetAdapters();
  if (adapters.empty()) {
    return;
  }
  if (adapters[0]->powered() == initial_powered) {
    return;
  }
  adapters[0]->set_powered(initial_powered, base::DoNothing());
}

void HandleDefaultHciInterfaceResponse(
    mojom::Executor* const executor,
    base::OnceCallback<void(mojom::ExecutedProcessResultPtr result)> on_finish,
    brillo::Error* error,
    int32_t hci_interface) {
  CHECK(executor);
  if (error) {
    LOG(ERROR) << "Failed to get default HCI interface.";
    std::move(on_finish).Run(mojom::ExecutedProcessResult::New(
        EXIT_FAILURE, /*out=*/"", /*err=*/error->GetMessage()));
    return;
  }
  executor->GetHciDeviceConfig(hci_interface, std::move(on_finish));
}

}  // namespace

BluetoothRoutineBase::BluetoothRoutineBase(Context* context)
    : context_(context) {
  CHECK(context_);
  adapters_ = context->bluez_controller()->GetAdapters();
}

BluetoothRoutineBase::~BluetoothRoutineBase() = default;

org::bluez::Adapter1ProxyInterface* BluetoothRoutineBase::GetAdapter() const {
  if (adapters_.empty()) {
    return nullptr;
  }
  return adapters_[0];
}

void BluetoothRoutineBase::EnsureAdapterPoweredState(
    bool powered, base::OnceCallback<void(bool)> on_finish) {
  if (!GetAdapter()) {
    std::move(on_finish).Run(false);
    return;
  }
  // Already on or off.
  if (powered == GetAdapter()->powered()) {
    std::move(on_finish).Run(true);
    return;
  }
  GetAdapter()->set_powered(powered, std::move(on_finish));
}

void BluetoothRoutineBase::RunPreCheck(
    base::OnceClosure on_passed,
    base::OnceCallback<void(mojom::DiagnosticRoutineStatusEnum status,
                            const std::string& error_message)> on_failed) {
  if (!GetAdapter()) {
    std::move(on_failed).Run(mojom::DiagnosticRoutineStatusEnum::kError,
                             kBluetoothRoutineFailedGetAdapter);
    return;
  }

  // Ensure the adapter is not in discovery mode. We should avoid running
  // Bluetooth routines when the adapter is actively scanning or pairing.
  bool initial_powered = GetAdapter()->powered();
  if (initial_powered && GetAdapter()->discovering()) {
    std::move(on_failed).Run(mojom::DiagnosticRoutineStatusEnum::kFailed,
                             kBluetoothRoutineFailedDiscoveryMode);
    return;
  }
  // Set up scoped closure runner for resetting adapter powered back to initial
  // powered state.
  reset_bluetooth_powered_ = base::ScopedClosureRunner(base::BindOnce(
      &ResetPoweredState, context_->bluez_controller(), initial_powered));

  std::move(on_passed).Run();
}

void BluetoothRoutineBase::GetDefaultHciConfig(
    base::OnceCallback<void(mojom::ExecutedProcessResultPtr result)>
        on_finish) {
  const auto manager = context_->floss_controller()->GetManager();
  if (!manager) {
    LOG(ERROR) << "Failed to access Bluetooth manager proxy.";
    std::move(on_finish).Run(mojom::ExecutedProcessResult::New(
        EXIT_FAILURE, /*out=*/"", /*err=*/"Bluetooth manager not found."));
    return;
  }
  auto [on_success, on_error] = SplitDbusCallback(
      base::BindOnce(&HandleDefaultHciInterfaceResponse, context_->executor(),
                     std::move(on_finish)));
  manager->GetDefaultAdapterAsync(std::move(on_success), std::move(on_error));
}

}  // namespace diagnostics::bluez
