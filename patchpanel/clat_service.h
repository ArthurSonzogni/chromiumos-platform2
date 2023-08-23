// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_CLAT_SERVICE_H_
#define PATCHPANEL_CLAT_SERVICE_H_

#include <optional>

#include <net-base/ipv6_address.h>

#include "patchpanel/shill_client.h"

namespace patchpanel {

// This class configures, starts or stops CLAT on ChromeOS host when the main
// Manager process notifies this class about changes on either the default
// logical device or IPConfig of it.
class ClatService {
 public:
  ClatService();
  ClatService(const ClatService&) = delete;
  ClatService& operator=(const ClatService&) = delete;

  virtual ~ClatService();

  // Processes changes in the default logical shill device.
  // This function judges whether CLAT is needed, and based on that decision it
  // will do one of the following operations: start CLAT, stop CLAT, reconfigure
  // and restart CLAT, or do nothing.
  void OnShillDefaultLogicalDeviceChanged(
      const ShillClient::Device* new_device,
      const ShillClient::Device* prev_device);

  // Processes changes in IPConfig of the default logical shill device.
  // This function judges whether CLAT is needed, and based on that decision it
  // will do one of the following operations: start CLAT, stop CLAT, reconfigure
  // and restart CLAT, or do nothing.
  void OnDefaultLogicalDeviceIPConfigChanged(
      const ShillClient::Device& default_logical_device);

 protected:
  // This function does the followings step by step: create a config file for
  // TAYGA, create a tun device, start TAYGA's process, add NDproxy, add IPRule
  // and IPRoute.
  virtual void StartClat(const ShillClient::Device& shill_device);

  // This function does the followings step by step: remove IPRule and IPRoute,
  // remove NDproxy, kill TAYGA's process, remove the tun device used for CLAT.
  virtual void StopClat();

  // These functions set or reset |clat_running_device_| in unit tests
  // respectively.
  void SetClatRunningDeviceForTest(const ShillClient::Device& shill_device);
  void ResetClatRunningDeviceForTest();

 private:
  bool IsClatRunningDevice(const ShillClient::Device& shill_device);

  std::optional<ShillClient::Device> clat_running_device_;
};
}  // namespace patchpanel

#endif  // PATCHPANEL_CLAT_SERVICE_H_
