// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swap_management/swap_management_dbus_adaptor.h"
#include "swap_management/swap_tool_metrics.h"

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
  swap_management::SwapToolMetrics::Get()->ReportSwapStartStatus(status);
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
  swap_management::SwapToolMetrics::Get()->ReportSwapStopStatus(status);
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

bool SwapManagementDBusAdaptor::SwapSetSize(brillo::ErrorPtr* error,
                                            int32_t size) {
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

bool SwapManagementDBusAdaptor::SwapSetSwappiness(brillo::ErrorPtr* error,
                                                  uint32_t swappiness) {
  ResetShutdownTimer();
  absl::Status status = swap_tool_->SwapSetSwappiness(swappiness);
  if (!status.ok()) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         "org.chromium.SwapManagement.error.SwapSetSwappiness",
                         status.ToString());
    return false;
  }
  return true;
}

std::string SwapManagementDBusAdaptor::SwapStatus() {
  ResetShutdownTimer();
  return swap_tool_->SwapStatus();
}

bool SwapManagementDBusAdaptor::SwapZramEnableWriteback(brillo::ErrorPtr* error,
                                                        uint32_t size_mb) {
  ResetShutdownTimer();
  absl::Status status = swap_tool_->SwapZramEnableWriteback(size_mb);
  if (!status.ok()) {
    brillo::Error::AddTo(
        error, FROM_HERE, brillo::errors::dbus::kDomain,
        "org.chromium.SwapManagement.error.SwapZramEnableWriteback",
        status.ToString());
    return false;
  }
  return true;
}

bool SwapManagementDBusAdaptor::SwapZramMarkIdle(brillo::ErrorPtr* error,
                                                 uint32_t age) {
  ResetShutdownTimer();
  absl::Status status = swap_tool_->SwapZramMarkIdle(age);
  if (!status.ok()) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         "org.chromium.SwapManagement.error.SwapZramMarkIdle",
                         status.ToString());
    return false;
  }
  return true;
}

bool SwapManagementDBusAdaptor::SwapZramSetWritebackLimit(
    brillo::ErrorPtr* error, uint32_t limit) {
  ResetShutdownTimer();
  absl::Status status = swap_tool_->SwapZramSetWritebackLimit(limit);
  if (!status.ok()) {
    brillo::Error::AddTo(
        error, FROM_HERE, brillo::errors::dbus::kDomain,
        "org.chromium.SwapManagement.error.SwapZramSetWritebackLimit",
        status.ToString());
    return false;
  }
  return true;
}

bool SwapManagementDBusAdaptor::InitiateSwapZramWriteback(
    brillo::ErrorPtr* error, uint32_t mode) {
  ResetShutdownTimer();
  absl::Status status = swap_tool_->InitiateSwapZramWriteback(mode);
  if (!status.ok()) {
    brillo::Error::AddTo(
        error, FROM_HERE, brillo::errors::dbus::kDomain,
        "org.chromium.SwapManagement.error.InitiateSwapZramWriteback",
        status.ToString());
    return false;
  }
  return true;
}

bool SwapManagementDBusAdaptor::MGLRUSetEnable(brillo::ErrorPtr* error,
                                               uint8_t value) {
  ResetShutdownTimer();
  absl::Status status = swap_tool_->MGLRUSetEnable(value);
  if (!status.ok()) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         "org.chromium.SwapManagement.error.MGLRUSetEnable",
                         status.ToString());
    return false;
  }
  return true;
}

}  // namespace swap_management
