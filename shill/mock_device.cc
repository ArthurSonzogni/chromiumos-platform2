// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/mock_device.h"

#include <string>

#include <base/memory/ref_counted.h>
#include <chromeos/net-base/mac_address.h>
#include <gmock/gmock.h>

namespace shill {

class ControlInterface;
class EventDispatcher;

using ::testing::DefaultValue;

MockDevice::MockDevice(Manager* manager,
                       const std::string& link_name,
                       net_base::MacAddress mac_address,
                       int interface_index)
    : Device(manager,
             link_name,
             mac_address,
             interface_index,
             Technology::kUnknown) {
  CreateImplicitNetwork(interface_index, link_name, /*fixed_ip_params=*/false);
  DefaultValue<Technology>::Set(Technology::kUnknown);
}

MockDevice::~MockDevice() = default;

}  // namespace shill
