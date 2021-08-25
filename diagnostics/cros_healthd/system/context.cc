// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/context.h"

#include <memory>
#include <utility>

#include <attestation/proto_bindings/interface.pb.h>
#include <attestation-client/attestation/dbus-proxies.h>
#include <base/logging.h>
#include <base/time/default_tick_clock.h>
#include <chromeos/chromeos-config/libcros_config/cros_config.h>
#include <chromeos/dbus/service_constants.h>
#include <cras/dbus-proxies.h>
#include <debugd/dbus-proxies.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>

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

Context::~Context() = default;

std::unique_ptr<Context> Context::Create(
    mojo::PlatformChannelEndpoint endpoint,
    std::unique_ptr<brillo::UdevMonitor>&& udev_monitor) {
  std::unique_ptr<Context> context(new Context());

  // Initiailize static member
  context->root_dir_ = base::FilePath("/");
  context->udev_monitor_ = std::move(udev_monitor);

  // Create and connect the adapter for the root-level executor.
  context->executor_ = std::make_unique<ExecutorAdapterImpl>();
  context->executor_->Connect(std::move(endpoint));

  // Initialize the D-Bus connection.
  auto dbus_bus = context->connection_.Connect();
  if (!dbus_bus) {
    LOG(ERROR) << "Failed to connect to the D-Bus system bus.";
    return nullptr;
  }

  // Create D-Bus clients:
  context->attestation_proxy_ =
      std::make_unique<org::chromium::AttestationProxy>(dbus_bus);
  context->bluetooth_client_ = std::make_unique<BluetoothClientImpl>(dbus_bus);
  context->cras_proxy_ = std::make_unique<org::chromium::cras::ControlProxy>(
      dbus_bus, cras::kCrasServiceName,
      dbus::ObjectPath(cras::kCrasServicePath));
  context->debugd_proxy_ =
      std::make_unique<org::chromium::debugdProxy>(dbus_bus);
  context->debugd_adapter_ = std::make_unique<DebugdAdapterImpl>(
      std::make_unique<org::chromium::debugdProxy>(dbus_bus));
  context->powerd_adapter_ = std::make_unique<PowerdAdapterImpl>(dbus_bus);
  context->tpm_manager_proxy_ =
      std::make_unique<org::chromium::TpmManagerProxy>(dbus_bus);

  // Create the mojo clients which will be initialized after connecting with
  // chrome.
  context->network_health_adapter_ =
      std::make_unique<NetworkHealthAdapterImpl>();
  context->network_diagnostics_adapter_ =
      std::make_unique<NetworkDiagnosticsAdapterImpl>();

  // Create others.
  auto cros_config = std::make_unique<brillo::CrosConfig>();
  if (!cros_config->Init()) {
    LOG(ERROR) << "Unable to initialize cros_config";
    return nullptr;
  }
  context->cros_config_ = std::move(cros_config);

  context->system_config_ = std::make_unique<SystemConfig>(
      context->cros_config_.get(), context->debugd_adapter_.get());
  context->system_utils_ = std::make_unique<SystemUtilitiesImpl>();
  context->tick_clock_ = std::make_unique<base::DefaultTickClock>();
  context->udev_ = std::make_unique<UdevImpl>();

  return context;
}

org::chromium::AttestationProxyInterface* Context::attestation_proxy() const {
  return attestation_proxy_.get();
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

const std::unique_ptr<brillo::UdevMonitor>& Context::udev_monitor() const {
  return udev_monitor_;
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

org::chromium::TpmManagerProxyInterface* Context::tpm_manager_proxy() const {
  return tpm_manager_proxy_.get();
}

UdevInterface* Context::udev() const {
  return udev_.get();
}

}  // namespace diagnostics
