// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "patchpanel/qos_service.h"

#include <base/containers/flat_set.h>

#include "patchpanel/datapath.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {

QoSService::QoSService(Datapath* datapath) : datapath_(datapath) {}

QoSService::~QoSService() = default;

void QoSService::Enable() {
  if (is_enabled_) {
    return;
  }
  is_enabled_ = true;

  datapath_->EnableQoSDetection();
  for (const auto& ifname : interfaces_) {
    datapath_->EnableQoSApplyingDSCP(ifname);
  }
}

void QoSService::Disable() {
  if (!is_enabled_) {
    return;
  }
  is_enabled_ = false;

  for (const auto& ifname : interfaces_) {
    datapath_->DisableQoSApplyingDSCP(ifname);
  }
  datapath_->DisableQoSDetection();
}

void QoSService::OnPhysicalDeviceAdded(const ShillClient::Device& device) {
  if (device.type != ShillClient::Device::Type::kWifi) {
    return;
  }
  if (!interfaces_.insert(device.ifname).second) {
    LOG(ERROR) << "Failed to start tracking " << device.ifname;
    return;
  }
  if (!is_enabled_) {
    return;
  }
  datapath_->EnableQoSApplyingDSCP(device.ifname);
}

void QoSService::OnPhysicalDeviceRemoved(const ShillClient::Device& device) {
  if (device.type != ShillClient::Device::Type::kWifi) {
    return;
  }
  if (interfaces_.erase(device.ifname) != 1) {
    LOG(ERROR) << "Failed to stop tracking " << device.ifname;
    return;
  }
  if (!is_enabled_) {
    return;
  }
  datapath_->DisableQoSApplyingDSCP(device.ifname);
}

}  // namespace patchpanel
