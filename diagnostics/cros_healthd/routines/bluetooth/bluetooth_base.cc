// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/bluetooth/bluetooth_base.h"

#include <utility>

#include <base/bind.h>

#include "diagnostics/cros_healthd/system/bluetooth_info_manager.h"

namespace diagnostics {

BluetoothRoutineBase::BluetoothRoutineBase(Context* context)
    : context_(context) {
  DCHECK(context_);
  adapters_ = context->bluetooth_info_manager()->GetAdapters();
}

BluetoothRoutineBase::~BluetoothRoutineBase() = default;

org::bluez::Adapter1ProxyInterface* BluetoothRoutineBase::GetAdapter() const {
  if (adapters_.empty())
    return nullptr;
  return adapters_[0];
}

void BluetoothRoutineBase::EnsureAdapterPoweredOn(
    base::OnceCallback<void(bool)> on_finish) {
  if (!GetAdapter()) {
    std::move(on_finish).Run(false);
    return;
  }
  // Already on.
  if (GetAdapter()->powered()) {
    std::move(on_finish).Run(true);
    return;
  }
  GetAdapter()->set_powered(true, std::move(on_finish));
}

}  // namespace diagnostics
