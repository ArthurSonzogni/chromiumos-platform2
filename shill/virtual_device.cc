// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/virtual_device.h"

#include <netinet/ether.h>
#include <linux/if.h>  // Needs definitions from netinet/ether.h

#include "shill/logging.h"
#include "shill/rtnl_handler.h"

using std::string;

namespace shill {

namespace {
const char kHardwareAddressEmpty[] = "";
}  // namespace {}

VirtualDevice::VirtualDevice(ControlInterface *control,
                             EventDispatcher *dispatcher,
                             Metrics *metrics,
                             Manager *manager,
                             const string &link_name,
                             int interface_index,
                             Technology::Identifier technology)
    : Device(control, dispatcher, metrics, manager, link_name,
             kHardwareAddressEmpty, interface_index, technology) {}

VirtualDevice::~VirtualDevice() {}

bool VirtualDevice::Load(StoreInterface */*storage*/) {
  // Virtual devices have no persistent state.
  return true;
}

bool VirtualDevice::Save(StoreInterface */*storage*/) {
  // Virtual devices have no persistent state.
  return true;
}

void VirtualDevice::Start(Error *error,
                          const EnabledStateChangedCallback &/*callback*/) {
  rtnl_handler()->SetInterfaceFlags(interface_index(), IFF_UP, IFF_UP);
  // TODO(quiche): Should we call OnEnabledStateChanged, like other Devices?
  if (error)
    error->Reset();  // indicate immediate completion
}

void VirtualDevice::Stop(Error *error,
                         const EnabledStateChangedCallback &/*callback*/) {
  // TODO(quiche): Should we call OnEnabledStateChanged, like other Devices?
  if (error)
    error->Reset();  // indicate immediate completion
}

void VirtualDevice::UpdateIPConfig(const IPConfig::Properties &properties) {
  SLOG(Device, 2) << __func__ << " on " << link_name();
  if (!ipconfig()) {
    set_ipconfig(new IPConfig(control_interface(), link_name()));
  }
  ipconfig()->set_properties(properties);
  OnIPConfigUpdated(ipconfig());
}

void VirtualDevice::DropConnection() {
  Device::DropConnection();
}

void VirtualDevice::SelectService(const ServiceRefPtr &service) {
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
