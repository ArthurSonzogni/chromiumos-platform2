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
#include <brillo/udev/udev.h>
#include <chromeos/chromeos-config/libcros_config/cros_config.h>
#include <chromeos/dbus/service_constants.h>
#include <cras/dbus-proxies.h>
#include <debugd/dbus-proxies.h>
#include <mojo/public/cpp/system/invitation.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>

#include "diagnostics/common/system/bluetooth_client_impl.h"
#include "diagnostics/common/system/debugd_adapter_impl.h"
#include "diagnostics/common/system/powerd_adapter_impl.h"
#include "diagnostics/cros_healthd/network/network_health_adapter_impl.h"
#include "diagnostics/cros_healthd/network_diagnostics/network_diagnostics_adapter_impl.h"
#include "diagnostics/cros_healthd/system/libdrm_util_impl.h"
#include "diagnostics/cros_healthd/system/pci_util_impl.h"
#include "diagnostics/cros_healthd/system/system_config.h"
#include "diagnostics/cros_healthd/system/system_utilities_impl.h"
#include "diagnostics/cros_healthd/utils/mojo_relay_impl.h"

namespace diagnostics {
namespace {
namespace executor_ipc = chromeos::cros_healthd_executor::mojom;

mojo::PendingRemote<executor_ipc::Executor> SendInvitationAndConnectToExecutor(
    mojo::PlatformChannelEndpoint endpoint) {
  // This sends invitation to the executor process. Must be the outgoing
  // invitation because cros_healthd is the process which connects to the mojo
  // broker. This must be run after the mojo ipc thread is initialized.
  mojo::OutgoingInvitation invitation;
  // Always use 0 as the default pipe name.
  mojo::ScopedMessagePipeHandle pipe = invitation.AttachMessagePipe(0);
  mojo::OutgoingInvitation::Send(std::move(invitation),
                                 base::kNullProcessHandle, std::move(endpoint));
  return mojo::PendingRemote<executor_ipc::Executor>(std::move(pipe),
                                                     /*version=*/0);
}
}  // namespace

Context::Context() = default;

Context::~Context() = default;

std::unique_ptr<Context> Context::Create(
    mojo::PlatformChannelEndpoint executor_endpoint,
    std::unique_ptr<brillo::UdevMonitor>&& udev_monitor) {
  std::unique_ptr<Context> context(new Context());

  // Initiailize static member
  context->root_dir_ = base::FilePath("/");
  context->udev_monitor_ = std::move(udev_monitor);

  // Connect to the root-level executor.
  context->executor_.Bind(
      SendInvitationAndConnectToExecutor(std::move(executor_endpoint)));

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
  context->internal_service_factory_relay_ =
      std::make_unique<MojoRelayImpl<chromeos::cros_healthd::internal::mojom::
                                         CrosHealthdInternalServiceFactory>>();
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
  context->udev_ = brillo::Udev::Create();

  return context;
}

std::unique_ptr<LibdrmUtil> Context::CreateLibdrmUtil() {
  return std::unique_ptr<LibdrmUtil>(new LibdrmUtilImpl());
}

std::unique_ptr<PciUtil> Context::CreatePciUtil() {
  return std::unique_ptr<PciUtil>(new PciUtilImpl());
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

chromeos::cros_healthd_executor::mojom::Executor* Context::executor() {
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

brillo::Udev* Context::udev() const {
  return udev_.get();
}

MojoRelay<
    chromeos::cros_healthd::internal::mojom::CrosHealthdInternalServiceFactory>*
Context::internal_service_factory_relay() {
  return internal_service_factory_relay_.get();
}

}  // namespace diagnostics
