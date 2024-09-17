// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swap_management/dbus_adaptor.h"

#include <memory>
#include <utility>
#include <vector>

#include <absl/status/status.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/dbus/swap_management/dbus-constants.h>
#include <dbus/object_path.h>
#include <featured/feature_library.h>
#include <power_manager/dbus-proxies.h>
#include <power_manager/proto_bindings/suspend.pb.h>

#include "swap_management/metrics.h"
#include "swap_management/suspend_history.h"

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

namespace {

// Handles the result of an attempt to connect to a D-Bus signal.
void HandleSignalConnected(const std::string& interface,
                           const std::string& signal,
                           bool success) {
  if (!success) {
    LOG(ERROR) << "Failed to connect to signal " << interface << "." << signal;
    return;
  }
  VLOG(2) << "Successfully connected to D-Bus signal " << interface << "."
          << signal;
}

void OnSuspendImminent(const std::vector<uint8_t>& data) {
  SuspendHistory::Get()->OnSuspendImminent();
}

void OnSuspendDone(const std::vector<uint8_t>& data) {
  power_manager::SuspendDone proto;
  if (!proto.ParseFromArray(data.data(), data.size())) {
    LOG(ERROR) << "Failed to parse suspend done signal";
  }
  SuspendHistory::Get()->OnSuspendDone(
      base::Microseconds(proto.suspend_duration()));
}

}  // namespace

void RegisterPowerManagerProxyHandlers(
    org::chromium::PowerManagerProxyInterface* power_manager_proxy) {
  power_manager_proxy->RegisterSuspendImminentSignalHandler(
      base::BindRepeating(&OnSuspendImminent),
      base::BindOnce(&HandleSignalConnected));
  power_manager_proxy->RegisterSuspendDoneSignalHandler(
      base::BindRepeating(&OnSuspendDone),
      base::BindOnce(&HandleSignalConnected));
}

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
