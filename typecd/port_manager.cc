// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/port_manager.h"

#include <base/logging.h>
#include <re2/re2.h>

namespace typecd {

void PortManager::OnPortAddedOrRemoved(const base::FilePath& path,
                                       int port_num,
                                       bool added) {
  auto it = ports_.find(port_num);
  if (added) {
    if (it != ports_.end()) {
      LOG(WARNING) << "Attempting to add an already added port.";
      return;
    }

    ports_.emplace(port_num, std::make_unique<Port>(path, port_num));
  } else {
    if (it == ports_.end()) {
      LOG(WARNING) << "Attempting to remove a non-existent port.";
      return;
    }

    ports_.erase(it);
  }
}

void PortManager::OnPartnerAddedOrRemoved(const base::FilePath& path,
                                          int port_num,
                                          bool added) {
  auto it = ports_.find(port_num);
  if (it == ports_.end()) {
    LOG(WARNING) << "Partner add/remove attempted for non-existent port "
                 << port_num;
    return;
  }

  auto port = it->second.get();
  if (added) {
    port->AddPartner(path);
  } else {
    port->RemovePartner();
  }
}

}  // namespace typecd
