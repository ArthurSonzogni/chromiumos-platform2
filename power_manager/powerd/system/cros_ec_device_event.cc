// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/cros_ec_device_event.h"

#include <base/files/file_util.h>

#include "power_manager/powerd/system/cros_ec_ioctl.h"

namespace power_manager {
namespace system {

void EnableCrosEcDeviceEvent(int event) {
  const uint32_t event_mask = EC_DEVICE_EVENT_MASK(event);
  struct ec_params_device_event p;
  struct ec_response_device_event* r;

  base::ScopedFD ec_fd =
      base::ScopedFD(open(cros_ec_ioctl::kCrosEcDevNodePath, O_RDWR));

  if (!ec_fd.is_valid()) {
    PLOG(ERROR) << "Failed to open " << cros_ec_ioctl::kCrosEcDevNodePath;
    return;
  }

  cros_ec_ioctl::IoctlCommand<struct ec_params_device_event,
                              struct ec_response_device_event>
      cmd(EC_CMD_DEVICE_EVENT);

  p.param = EC_DEVICE_EVENT_PARAM_GET_ENABLED_EVENTS;
  cmd.SetReq(p);
  if (!cmd.Run(ec_fd.get())) {
    LOG(ERROR) << "Failed to get CrOS EC device event mask";
    return;
  }

  r = cmd.Resp();

  /* Return if mask is already enabled. */
  if (r->event_mask & event_mask)
    return;

  p.param = EC_DEVICE_EVENT_PARAM_SET_ENABLED_EVENTS;
  p.event_mask = r->event_mask | event_mask;
  cmd.SetReq(p);
  if (!cmd.Run(ec_fd.get())) {
    LOG(ERROR) << "Failed to set CrOS EC device event for " << event;
    return;
  }

  r = cmd.Resp();
  if (!(r->event_mask & event_mask)) {
    LOG(ERROR) << "Failed to enable CrOS EC device event for " << event;
    return;
  }

  LOG(INFO) << "CrOS EC device event is enabled for " << event;
}

}  // namespace system
}  // namespace power_manager
