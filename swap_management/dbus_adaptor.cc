// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "featured/feature_library.h"
#include "swap_management/dbus_adaptor.h"
#include "swap_management/metrics.h"

#include <memory>
#include <utility>

#include <absl/status/status.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/dbus/swap_management/dbus-constants.h>
#include <dbus/object_path.h>

namespace {
feature::PlatformFeatures* GetPlatformFeatures() {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus(new dbus::Bus(options));

  if (!feature::PlatformFeatures::Initialize(bus)) {
    LOG(WARNING) << "Unable to initialize PlatformFeatures framework, will not "
                    "be able to check for system flags.";
    return nullptr;
  }

  return feature::PlatformFeatures::Get();
}
}  // namespace

namespace swap_management {

DBusAdaptor::DBusAdaptor(scoped_refptr<dbus::Bus> bus)
    : org::chromium::SwapManagementAdaptor(this),
      dbus_object_(nullptr, bus, dbus::ObjectPath(kSwapManagementServicePath)),
      swap_tool_(std::make_unique<SwapTool>(GetPlatformFeatures())) {}

void DBusAdaptor::RegisterAsync(
    brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb) {
  auto* my_interface = dbus_object_.AddOrGetInterface(kSwapManagementInterface);
  DCHECK(my_interface);
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(std::move(cb));
}

bool DBusAdaptor::SwapStart(brillo::ErrorPtr* error) {
  absl::Status status = swap_tool_->SwapStart();
  swap_management::Metrics::Get()->ReportSwapStartStatus(status);
  if (!status.ok()) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         "org.chromium.SwapManagement.error.SwapStart",
                         status.ToString());
    return false;
  }
  return true;
}

bool DBusAdaptor::SwapStop(brillo::ErrorPtr* error) {
  absl::Status status = swap_tool_->SwapStop();
  swap_management::Metrics::Get()->ReportSwapStopStatus(status);
  if (!status.ok()) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         "org.chromium.SwapManagement.error.SwapStop",
                         status.ToString());
    return false;
  }
  return true;
}

bool DBusAdaptor::SwapRestart(brillo::ErrorPtr* error) {
  return SwapStop(error) && SwapStart(error);
}

bool DBusAdaptor::SwapSetSize(brillo::ErrorPtr* error, int32_t size) {
  absl::Status status = swap_tool_->SwapSetSize(size);
  if (!status.ok()) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         "org.chromium.SwapManagement.error.SwapSetSize",
                         status.ToString());
    return false;
  }

  return true;
}

bool DBusAdaptor::SwapSetSwappiness(brillo::ErrorPtr* error,
                                    uint32_t swappiness) {
  absl::Status status = swap_tool_->SwapSetSwappiness(swappiness);
  if (!status.ok()) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         "org.chromium.SwapManagement.error.SwapSetSwappiness",
                         status.ToString());
    return false;
  }
  return true;
}

std::string DBusAdaptor::SwapStatus() {
  return swap_tool_->SwapStatus();
}

bool DBusAdaptor::SwapZramEnableWriteback(brillo::ErrorPtr* error,
                                          uint32_t size_mb) {
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

bool DBusAdaptor::SwapZramMarkIdle(brillo::ErrorPtr* error, uint32_t age) {
  absl::Status status = swap_tool_->SwapZramMarkIdle(age);
  if (!status.ok()) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         "org.chromium.SwapManagement.error.SwapZramMarkIdle",
                         status.ToString());
    return false;
  }
  return true;
}

bool DBusAdaptor::SwapZramSetWritebackLimit(brillo::ErrorPtr* error,
                                            uint32_t limit) {
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

bool DBusAdaptor::InitiateSwapZramWriteback(brillo::ErrorPtr* error,
                                            uint32_t mode) {
  absl::Status status = swap_tool_->InitiateSwapZramWriteback(
      static_cast<ZramWritebackMode>(mode));
  if (!status.ok()) {
    brillo::Error::AddTo(
        error, FROM_HERE, brillo::errors::dbus::kDomain,
        "org.chromium.SwapManagement.error.InitiateSwapZramWriteback",
        status.ToString());
    return false;
  }
  return true;
}

bool DBusAdaptor::MGLRUSetEnable(brillo::ErrorPtr* error, uint8_t value) {
  absl::Status status = swap_tool_->MGLRUSetEnable(value);
  if (!status.ok()) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         "org.chromium.SwapManagement.error.MGLRUSetEnable",
                         status.ToString());
    return false;
  }
  return true;
}

bool DBusAdaptor::ReclaimAllProcesses(brillo::ErrorPtr* error,
                                      uint8_t memory_types) {
  absl::Status status = swap_tool_->ReclaimAllProcesses(memory_types);
  if (!status.ok()) {
    brillo::Error::AddTo(
        error, FROM_HERE, brillo::errors::dbus::kDomain,
        "org.chromium.SwapManagement.error.ReclaimAllProcesses",
        status.ToString());
    return false;
  }
  return true;
}

}  // namespace swap_management
