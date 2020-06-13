// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_PORT_MANAGER_H_
#define TYPECD_PORT_MANAGER_H_

#include <map>
#include <memory>
#include <utility>

#include <gtest/gtest_prod.h>

#include "typecd/port.h"
#include "typecd/udev_monitor.h"

namespace typecd {

// This class is used to manage Type C ports and related state. It's role is to
// provide the daemon with an accurate view of the Type C state (after reading
// from the Type C connector class framework sysfs files), as well as provide a
// means to change this state according to policy defined in the daemon.
class PortManager : public UdevMonitor::Observer {
 public:
  PortManager() = default;

 private:
  // UdevMonitor::Observer overrides.
  void OnPortAddedOrRemoved(const base::FilePath& path,
                            int port_num,
                            bool added) override;
  void OnPartnerAddedOrRemoved(const base::FilePath& path,
                               int port_num,
                               bool added) override;

  std::map<int, std::unique_ptr<Port>> ports_;

  DISALLOW_COPY_AND_ASSIGN(PortManager);
};

}  // namespace typecd

#endif  // TYPECD_PORT_MANAGER_H_
