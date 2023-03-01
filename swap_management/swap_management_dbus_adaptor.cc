// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swap_management/swap_management_dbus_adaptor.h"

#include <memory>
#include <utility>

#include <absl/status/status.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/object_path.h>

namespace swap_management {
SwapManagementDBusAdaptor::SwapManagementDBusAdaptor(
    scoped_refptr<dbus::Bus> bus,
    std::unique_ptr<base::OneShotTimer> shutdown_timer)
    : org::chromium::SwapManagementAdaptor(this),
      dbus_object_(nullptr, bus, dbus::ObjectPath(kSwapManagementServicePath)),
      swap_tool_(std::make_unique<SwapTool>()),
      shutdown_timer_(std::move(shutdown_timer)) {}

SwapManagementDBusAdaptor::~SwapManagementDBusAdaptor() {
  if (shutdown_timer_)
    shutdown_timer_->Stop();
}

void SwapManagementDBusAdaptor::RegisterAsync(
    brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb) {
  auto* my_interface = dbus_object_.AddOrGetInterface(kSwapManagementInterface);
  DCHECK(my_interface);
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(std::move(cb));
}

void SwapManagementDBusAdaptor::ResetShutdownTimer() {
  if (shutdown_timer_)
    shutdown_timer_->Reset();
}

bool SwapManagementDBusAdaptor::SwapStart(brillo::ErrorPtr* error) {
  ResetShutdownTimer();
  absl::Status status = swap_tool_->SwapStart();
  if (!status.ok()) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         "org.chromium.SwapManagement.error.SwapStart",
                         status.ToString());
    return false;
  }
  return true;
}

bool SwapManagementDBusAdaptor::SwapStop(brillo::ErrorPtr* error) {
  ResetShutdownTimer();
  absl::Status status = swap_tool_->SwapStop();
  if (!status.ok()) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         "org.chromium.SwapManagement.error.SwapStop",
                         status.ToString());
    return false;
  }
  return true;
}

bool SwapManagementDBusAdaptor::SwapRestart(brillo::ErrorPtr* error) {
  return SwapStop(error) && SwapStart(error);
}

bool SwapManagementDBusAdaptor::MGLRUSetEnable(brillo::ErrorPtr* error,
                                               bool enable,
                                               bool* out_result) {
  ResetShutdownTimer();
  *out_result = swap_tool_->MGLRUSetEnable(error, enable);
  return *out_result;
}

bool SwapManagementDBusAdaptor::SwapSetSize(brillo::ErrorPtr* error,
                                            uint32_t size) {
  ResetShutdownTimer();
  absl::Status status = swap_tool_->SwapSetSize(size);
  if (!status.ok()) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         "org.chromium.SwapManagement.error.SwapSetSize",
                         status.ToString());
    return false;
  }

  return true;
}

std::string SwapManagementDBusAdaptor::SwapStatus() {
  ResetShutdownTimer();
  return swap_tool_->SwapStatus();
}

std::string SwapManagementDBusAdaptor::SwapZramEnableWriteback(
    uint32_t size_mb) {
  ResetShutdownTimer();
  return swap_tool_->SwapZramEnableWriteback(size_mb);
}

std::string SwapManagementDBusAdaptor::SwapZramMarkIdle(uint32_t age) {
  ResetShutdownTimer();
  return swap_tool_->SwapZramMarkIdle(age);
}

std::string SwapManagementDBusAdaptor::SwapZramSetWritebackLimit(
    uint32_t limit) {
  ResetShutdownTimer();
  return swap_tool_->SwapZramSetWritebackLimit(limit);
}

std::string SwapManagementDBusAdaptor::InitiateSwapZramWriteback(
    uint32_t mode) {
  ResetShutdownTimer();
  return swap_tool_->InitiateSwapZramWriteback(mode);
}

}  // namespace swap_management
