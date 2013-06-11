// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_VIRTUAL_DEVICE_H_
#define SHILL_VIRTUAL_DEVICE_H_

#include <base/basictypes.h>

#include "shill/device.h"
#include "shill/ipconfig.h"
#include "shill/technology.h"

namespace shill {

class StorageInterface;

// A VirtualDevice represents a device that doesn't provide its own
// physical layer. This includes, e.g., tunnel interfaces used for
// OpenVPN, and PPP devices used for L2TPIPSec and 3G PPP dongles.
// (PPP devices are represented via the PPPDevice subclass.)
class VirtualDevice : public Device {
 public:
  VirtualDevice(ControlInterface *control,
                EventDispatcher *dispatcher,
                Metrics *metrics,
                Manager *manager,
                const std::string &link_name,
                int interface_index,
                Technology::Identifier technology);
  virtual ~VirtualDevice() override;

  virtual bool Load(StoreInterface *storage) override;
  virtual bool Save(StoreInterface *storage) override;

  virtual void Start(Error *error,
                     const EnabledStateChangedCallback &callback) override;
  virtual void Stop(Error *error,
                    const EnabledStateChangedCallback &callback) override;

  virtual void UpdateIPConfig(const IPConfig::Properties &properties);

  // Expose protected device methods to manager of this device.
  // (E.g. Cellular, L2TPIPSecDriver, OpenVPNDriver.)
  virtual void DropConnection();
  virtual void SelectService(const ServiceRefPtr &service);

 private:
  DISALLOW_COPY_AND_ASSIGN(VirtualDevice);
};

}  // namespace shill

#endif  // SHILL_VIRTUAL_DEVICE_H_
