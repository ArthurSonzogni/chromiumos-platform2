// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include "runtime_probe/system/context.h"

#include <base/logging.h>
#include <base/no_destructor.h>
#include <debugd/dbus-proxies.h>

#include <diagnostics/mojom/public/cros_healthd.mojom.h>
#include <diagnostics/mojom/public/cros_healthd_probe.mojom.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo_service_manager/lib/connect.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>

#include "runtime_probe/system/context_impl.h"

namespace runtime_probe {

namespace {

constexpr char kCrosHealthdProbeServiceName[] = "CrosHealthdProbe";

chromeos::mojo_service_manager::mojom::ServiceManager*
GetServiceManagerProxy() {
  static const base::NoDestructor<
      mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>>
      remote(chromeos::mojo_service_manager::ConnectToMojoServiceManager());

  CHECK(remote->is_bound()) << "Failed to connect to mojo service manager.";
  return remote->get();
}

}  // namespace

// Define the constructor here instead of the header to allow us using forward
// declaration in headers. This can reduce the dependencies to external
// libraries like debugd-client for the components which don't use them.
ContextImpl::ContextImpl() = default;
ContextImpl::~ContextImpl() = default;

bool ContextImpl::SetupDBusServices() {
  dbus_bus_ = connection_.Connect();
  if (!dbus_bus_) {
    LOG(ERROR) << "Cannot connect to dbus.";
    return false;
  }
  debugd_proxy_ = std::make_unique<org::chromium::debugdProxy>(dbus_bus_);
  shill_manager_proxy_ =
      std::make_unique<org::chromium::flimflam::ManagerProxy>(dbus_bus_);
  return true;
}

cros_healthd_mojom::CrosHealthdProbeService*
ContextImpl::GetCrosHealthdProbeServiceProxy() {
  if (!cros_healthd_service_.is_bound()) {
    GetServiceManagerProxy()->Request(
        kCrosHealthdProbeServiceName, std::nullopt,
        cros_healthd_service_.BindNewPipeAndPassReceiver().PassPipe());
    cros_healthd_service_.set_disconnect_handler(base::BindOnce(
        []() { VLOG(2) << "Disconnect from the cros_healthd service."; }));
    cros_healthd_service_.reset_on_disconnect();
  }
  return cros_healthd_service_.get();
}

}  // namespace runtime_probe
