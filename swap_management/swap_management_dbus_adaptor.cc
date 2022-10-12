// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swap_management/swap_management_dbus_adaptor.h"

#include <memory>
#include <utility>

#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/object_path.h>

namespace swap_management {
SwapManagementDBusAdaptor::SwapManagementDBusAdaptor(
    scoped_refptr<dbus::Bus> bus)
    : org::chromium::SwapManagementAdaptor(this),
      dbus_object_(nullptr, bus, dbus::ObjectPath(kSwapManagementServicePath)),
      swap_tool_(std::make_unique<SwapTool>()) {}

void SwapManagementDBusAdaptor::RegisterAsync(
    brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb) {
  auto* my_interface = dbus_object_.AddOrGetInterface(kSwapManagementInterface);
  DCHECK(my_interface);
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(std::move(cb));
}

bool SwapManagementDBusAdaptor::MGLRUSetEnable(brillo::ErrorPtr* error,
                                               bool enable,
                                               bool* out_result) {
  *out_result = swap_tool_->MGLRUSetEnable(error, enable);
  return *out_result;
}

std::string SwapManagementDBusAdaptor::SwapEnable(int32_t size,
                                                  bool change_now) {
  return swap_tool_->SwapEnable(size, change_now);
}

std::string SwapManagementDBusAdaptor::SwapDisable(bool change_now) {
  return swap_tool_->SwapDisable(change_now);
}

std::string SwapManagementDBusAdaptor::SwapStartStop(bool on) {
  return swap_tool_->SwapStartStop(on);
}

std::string SwapManagementDBusAdaptor::SwapStatus() {
  return swap_tool_->SwapStatus();
}

std::string SwapManagementDBusAdaptor::SwapSetParameter(
    const std::string& parameter_name, uint32_t parameter_value) {
  return swap_tool_->SwapSetParameter(parameter_name, parameter_value);
}

std::string SwapManagementDBusAdaptor::SwapZramEnableWriteback(
    uint32_t size_mb) {
  return swap_tool_->SwapZramEnableWriteback(size_mb);
}

std::string SwapManagementDBusAdaptor::SwapZramMarkIdle(uint32_t age) {
  return swap_tool_->SwapZramMarkIdle(age);
}

std::string SwapManagementDBusAdaptor::SwapZramSetWritebackLimit(
    uint32_t limit) {
  return swap_tool_->SwapZramSetWritebackLimit(limit);
}

std::string SwapManagementDBusAdaptor::InitiateSwapZramWriteback(
    uint32_t mode) {
  return swap_tool_->InitiateSwapZramWriteback(mode);
}

}  // namespace swap_management
