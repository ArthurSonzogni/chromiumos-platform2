// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WIMAX_MANAGER_GDM_DRIVER_H_
#define WIMAX_MANAGER_GDM_DRIVER_H_

extern "C" {
#include <gct/gctapi.h>
}  // extern "C"

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include <base/macros.h>
#include <base/memory/weak_ptr.h>

#include "wimax_manager/driver.h"
#include "wimax_manager/network.h"

namespace wimax_manager {

class GdmDevice;
class Manager;

class GdmDriver : public Driver,
                  public base::SupportsWeakPtr<GdmDriver> {
 public:
  explicit GdmDriver(Manager *manager);
  ~GdmDriver() override;

  bool Initialize() override;
  bool Finalize() override;
  bool GetDevices(std::vector<std::unique_ptr<Device>> *devices) override;

  bool OpenDevice(GdmDevice *device);
  bool CloseDevice(GdmDevice *device);
  bool GetDeviceStatus(GdmDevice *device);
  bool GetDeviceRFInfo(GdmDevice *device);
  bool SetDeviceEAPParameters(GdmDevice *device,
                              GCT_API_EAP_PARAM *eap_parameters);
  bool AutoSelectProfileForDevice(GdmDevice *device);
  bool PowerOnDeviceRF(GdmDevice *device);
  bool PowerOffDeviceRF(GdmDevice *device);
  bool SetScanInterval(GdmDevice *device, uint32_t interval);
  bool GetNetworksForDevice(GdmDevice *device,
                            std::vector<NetworkRefPtr> *networks);
  bool ConnectDeviceToNetwork(GdmDevice *device, const Network &network);
  bool DisconnectDeviceFromNetwork(GdmDevice *device);

 private:
  bool CreateInitialDirectories() const;
  GDEV_ID GetDeviceId(const GdmDevice *device) const;

  APIHAND api_handle_;

  DISALLOW_COPY_AND_ASSIGN(GdmDriver);
};

}  // namespace wimax_manager

#endif  // WIMAX_MANAGER_GDM_DRIVER_H_
