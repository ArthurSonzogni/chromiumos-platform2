// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SYSTEM_CROS_DISKS_CLIENT_IMPL_H_
#define RMAD_SYSTEM_CROS_DISKS_CLIENT_IMPL_H_

#include "rmad/system/cros_disks_client.h"

#include <string>
#include <vector>

#include <base/callback.h>
#include <base/memory/scoped_refptr.h>
#include <dbus/bus.h>
#include <dbus/object_proxy.h>

namespace rmad {

class CrosDisksClientImpl : public CrosDisksClient {
 public:
  explicit CrosDisksClientImpl(const scoped_refptr<dbus::Bus>& bus);
  CrosDisksClientImpl(const CrosDisksClientImpl&) = delete;
  CrosDisksClientImpl& operator=(const CrosDisksClientImpl&) = delete;

  ~CrosDisksClientImpl() override = default;

  bool EnumerateDevices(std::vector<std::string>* devices) override;
  bool GetDeviceProperties(const std::string& device,
                           DeviceProperties* device_properties) override;

  void AddMountCompletedHandler(
      base::RepeatingCallback<void(const MountEntry&)> callback) override;
  void Mount(const std::string& source,
             const std::string& filesystem_type,
             const std::vector<std::string>& options) override;
  bool Unmount(const std::string& path,
               const std::vector<std::string>& options,
               uint32_t* result) override;

 private:
  // Owned by external D-Bus bus.
  dbus::ObjectProxy* proxy_;
};

}  // namespace rmad

#endif  // RMAD_SYSTEM_CROS_DISKS_CLIENT_IMPL_H_
