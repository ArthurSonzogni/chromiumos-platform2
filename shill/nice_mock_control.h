// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NICE_MOCK_CONTROL_H_
#define SHILL_NICE_MOCK_CONTROL_H_

#include <base/basictypes.h>

#include "shill/control_interface.h"

namespace shill {
// An implementation of the Shill RPC-channel-interface-factory interface that
// returns nice mocks.
class NiceMockControl : public ControlInterface {
 public:
  NiceMockControl();
  ~NiceMockControl() override;

  // Each of these can be called once.  Ownership of the appropriate
  // interface pointer is given up upon call.
  virtual DeviceAdaptorInterface *CreateDeviceAdaptor(Device *device);
  virtual IPConfigAdaptorInterface *CreateIPConfigAdaptor(IPConfig *config);
  virtual ManagerAdaptorInterface *CreateManagerAdaptor(Manager *manager);
  virtual ProfileAdaptorInterface *CreateProfileAdaptor(Profile *profile);
  virtual RPCTaskAdaptorInterface *CreateRPCTaskAdaptor(RPCTask *task);
  virtual ServiceAdaptorInterface *CreateServiceAdaptor(Service *service);

 private:
  DISALLOW_COPY_AND_ASSIGN(NiceMockControl);
};

}  // namespace shill

#endif  // SHILL_NICE_MOCK_CONTROL_H_
