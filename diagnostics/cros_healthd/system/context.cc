// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/context.h"

#include <utility>

#include <base/logging.h>
#include <base/time/default_tick_clock.h>
#include <chromeos/chromeos-config/libcros_config/cros_config.h>
#include <chromeos/dbus/service_constants.h>

#include "cras/dbus-proxies.h"
#include "debugd/dbus-proxies.h"
#include "diagnostics/common/system/bluetooth_client_impl.h"
#include "diagnostics/common/system/debugd_adapter_impl.h"
#include "diagnostics/common/system/powerd_adapter_impl.h"
#include "diagnostics/cros_healthd/executor/executor_adapter_impl.h"
#include "diagnostics/cros_healthd/network/network_health_adapter_impl.h"
#include "diagnostics/cros_healthd/network_diagnostics/network_diagnostics_adapter_impl.h"
#include "diagnostics/cros_healthd/system/system_config.h"
#include "diagnostics/cros_healthd/system/system_utilities_impl.h"
#include "diagnostics/cros_healthd/system/udev_impl.h"

namespace diagnostics {

Context::Context() = default;

Context::Context(mojo::PlatformChannelEndpoint endpoint)
    : endpoint_(std::move(endpoint)), root_dir_(base::FilePath("/")) {}

Context::~Context() = default;

bool Context::Initialize() {
  // Initialize the D-Bus connection.
  dbus_bus_ = connection_.Connect();
  if (!dbus_bus_) {
    LOG(ERROR) << "Failed to connect to the D-Bus system bus.";
    return false;
  }

  // Initialize D-Bus clients:
  bluetooth_client_ = std::make_unique<BluetoothClientImpl>(dbus_bus_);
  cras_proxy_ = std::make_unique<org::chromium::cras::ControlProxy>(
      dbus_bus_, cras::kCrasServiceName,
      dbus::ObjectPath(cras::kCrasServicePath));
  debugd_proxy_ = std::make_unique<org::chromium::debugdProxy>(dbus_bus_);
  debugd_adapter_ = std::make_unique<DebugdAdapterImpl>(
      std::make_unique<org::chromium::debugdProxy>(dbus_bus_));
  // Create the NetworkHealthAdapter.
  network_health_adapter_ = std::make_unique<NetworkHealthAdapterImpl>();
  // Create the NetworkDiagnosticsAdapter.
  network_diagnostics_adapter_ =
      std::make_unique<NetworkDiagnosticsAdapterImpl>();
  powerd_adapter_ = std::make_unique<PowerdAdapterImpl>(dbus_bus_);

  cros_config_ = std::make_unique<brillo::CrosConfig>();

  // Init should always succeed.
  if (!static_cast<brillo::CrosConfig*>(cros_config_.get())->Init()) {
    LOG(ERROR) << "Unable to initialize cros_config";
    return false;
  }

  system_config_ =
      std::make_unique<SystemConfig>(cros_config_.get(), debugd_adapter_.get());
  system_utils_ = std::make_unique<SystemUtilitiesImpl>();

  // Create and connect the adapter for the root-level executor.
  executor_ = std::make_unique<ExecutorAdapterImpl>();
  executor_->Connect(std::move(endpoint_));

  tick_clock_ = std::make_unique<base::DefaultTickClock>();

  udev_ = std::make_unique<UdevImpl>();

  return true;
}

BluetoothClient* Context::bluetooth_client() const {
  return bluetooth_client_.get();
}

brillo::CrosConfigInterface* Context::cros_config() const {
  return cros_config_.get();
}

org::chromium::debugdProxyInterface* Context::debugd_proxy() const {
  return debugd_proxy_.get();
}

org::chromium::cras::ControlProxyInterface* Context::cras_proxy() const {
  return cras_proxy_.get();
}

DebugdAdapter* Context::debugd_adapter() const {
  return debugd_adapter_.get();
}

NetworkHealthAdapter* Context::network_health_adapter() const {
  return network_health_adapter_.get();
}

NetworkDiagnosticsAdapter* Context::network_diagnostics_adapter() const {
  return network_diagnostics_adapter_.get();
}

PowerdAdapter* Context::powerd_adapter() const {
  return powerd_adapter_.get();
}

const base::FilePath& Context::root_dir() const {
  return root_dir_;
}

const base::Time Context::time() const {
  return base::Time().Now();
}

SystemConfigInterface* Context::system_config() const {
  return system_config_.get();
}

ExecutorAdapter* Context::executor() const {
  return executor_.get();
}

SystemUtilities* Context::system_utils() const {
  return system_utils_.get();
}

base::TickClock* Context::tick_clock() const {
  return tick_clock_.get();
}

UdevInterface* Context::udev() const {
  return udev_.get();
}

}  // namespace diagnostics
