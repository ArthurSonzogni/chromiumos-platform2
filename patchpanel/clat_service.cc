// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/clat_service.h"

#include <base/logging.h>
#include <optional>

#include "base/check.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {

// TODO(b/278970851): Do the actual implementation. ClatService class needs to
// take a Datapath* argument in constructor.
ClatService::ClatService() = default;

// TODO(b/278970851): Do the actual implementation
ClatService::~ClatService() {
  StopClat();
}

void ClatService::Enable() {
  if (is_enabled_) {
    return;
  }

  is_enabled_ = true;
  // Starts CLAT immediately, if the default network is CLAT-eligible when it
  // gets enabled.
  if (clat_running_device_) {
    StartClat(clat_running_device_.value());
  }
}

void ClatService::Disable() {
  if (!is_enabled_) {
    return;
  }
  is_enabled_ = false;

  // We keep `clat_running_device_` here because we want to start CLAT
  // immediately after the feature is enabled again.
  StopClat(/*clear_running_device=*/false);
}

void ClatService::OnShillDefaultLogicalDeviceChanged(
    const ShillClient::Device* new_device,
    const ShillClient::Device* prev_device) {
  bool need_stop =
      clat_running_device_ && !(new_device && IsClatRunningDevice(*new_device));

  if (need_stop) {
    StopClat();
  }

  // CLAT should be started when CLAT is not running and the new default logical
  // device is IPv6-only.
  bool need_start =
      new_device && !clat_running_device_ && new_device->IsIPv6Only();

  if (need_start) {
    StartClat(*new_device);
  }

  return;
}

// TODO(b/278970851): Add delay between the occurrence of this event and the
// execution of StartClat().
// https://chromium-review.googlesource.com/c/chromiumos/platform2/+/4803285/comment/ff1aa754_26e63d28/
void ClatService::OnDefaultLogicalDeviceIPConfigChanged(
    const ShillClient::Device& default_logical_device) {
  if (!clat_running_device_) {
    if (default_logical_device.IsIPv6Only()) {
      StartClat(default_logical_device);
    }
    return;
  }

  // It is unexpected that CLAT is running on the device other than the default
  // logical device.
  if (!IsClatRunningDevice(default_logical_device)) {
    LOG(ERROR) << "CLAT is running on the device " << clat_running_device_
               << " although the default logical device is "
               << default_logical_device.ifname;
    StopClat();
    return;
  }

  // CLAT is running on the default logical device.

  DCHECK(!clat_running_device_->ipconfig.ipv4_cidr.has_value());
  DCHECK(clat_running_device_->ipconfig.ipv6_cidr.has_value());

  if (!default_logical_device.IsIPv6Only()) {
    StopClat();
    return;
  }

  if (clat_running_device_->ipconfig.ipv6_cidr !=
      default_logical_device.ipconfig.ipv6_cidr) {
    // TODO(b/278970851): Optimize the restart process of CLAT. Resources
    // such as the tun device can be reused.
    StopClat();

    StartClat(default_logical_device);
  }
}

void ClatService::StartClat(const ShillClient::Device& shill_device) {
  // Even if CLAT is disabled, we keep track of the device on which CLAT
  // should be running so that we can start CLAT immediately after it's
  // enabled.
  clat_running_device_ = shill_device;

  if (!is_enabled_) {
    return;
  }

  // TODO(b/278970851): Do the actual implementation
  // Doing network configurations for CLAT and starting CLAT daemon.
}

void ClatService::StopClat(bool clear_running_device) {
  if (!is_enabled_) {
    if (clear_running_device) {
      clat_running_device_.reset();
    }
    return;
  }

  if (!clat_running_device_) {
    return;
  }

  // TODO(b/278970851): Do the actual implementation
  // Cleaning up network configurations for CLAT.

  if (clear_running_device) {
    clat_running_device_.reset();
  }
}

void ClatService::SetClatRunningDeviceForTest(
    const ShillClient::Device& shill_device) {
  clat_running_device_ = shill_device;
}

void ClatService::ResetClatRunningDeviceForTest() {
  clat_running_device_.reset();
}

bool ClatService::IsClatRunningDevice(const ShillClient::Device& shill_device) {
  if (!clat_running_device_) {
    return false;
  }

  return shill_device.ifname == clat_running_device_->ifname;
}

}  // namespace patchpanel
