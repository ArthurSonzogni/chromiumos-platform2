// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/virtual_device.h"

#include <netinet/ether.h>
#include <linux/if.h>  // NOLINT - Needs definitions from netinet/ether.h

#include <memory>
#include <string>

#include "shill/logging.h"
#include "shill/net/rtnl_handler.h"

#include <base/logging.h>

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDevice;
}  // namespace Logging

namespace {
const char kHardwareAddressEmpty[] = "";
}  // namespace

VirtualDevice::VirtualDevice(Manager* manager,
                             const std::string& link_name,
                             int interface_index,
                             Technology technology,
                             bool fixed_ip_params)
    : Device(manager,
             link_name,
             kHardwareAddressEmpty,
             interface_index,
             technology,
             fixed_ip_params) {}

VirtualDevice::~VirtualDevice() = default;

bool VirtualDevice::Load(const StoreInterface* /*storage*/) {
  // Virtual devices have no persistent state.
  return true;
}

bool VirtualDevice::Save(StoreInterface* /*storage*/) {
  // Virtual devices have no persistent state.
  return true;
}

void VirtualDevice::Start(const EnabledStateChangedCallback& callback) {
  if (!network()->fixed_ip_params()) {
    rtnl_handler()->SetInterfaceFlags(interface_index(), IFF_UP, IFF_UP);
  }
  callback.Run(Error(Error::kSuccess));
}

void VirtualDevice::Stop(const EnabledStateChangedCallback& callback) {
  callback.Run(Error(Error::kSuccess));
}

void VirtualDevice::UpdateIPConfig(const IPConfig::Properties& properties) {
  SLOG(2) << __func__ << " on " << link_name();
  network()->set_link_protocol_ipv4_properties(
      std::make_unique<IPConfig::Properties>(properties));
  network()->Start(Network::StartOptions{
      .dhcp = std::nullopt,
      .accept_ra = false,
  });
}

void VirtualDevice::DropConnection() {
  Device::DropConnection();
}

void VirtualDevice::SelectService(const ServiceRefPtr& service) {
  Device::SelectService(service);
}

void VirtualDevice::SetServiceState(Service::ConnectState state) {
  Device::SetServiceState(state);
}

void VirtualDevice::SetServiceFailure(Service::ConnectFailure failure_state) {
  Device::SetServiceFailure(failure_state);
}

void VirtualDevice::SetServiceFailureSilent(
    Service::ConnectFailure failure_state) {
  Device::SetServiceFailureSilent(failure_state);
}

}  // namespace shill
