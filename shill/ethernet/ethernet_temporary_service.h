// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_ETHERNET_ETHERNET_TEMPORARY_SERVICE_H_
#define SHILL_ETHERNET_ETHERNET_TEMPORARY_SERVICE_H_

#include <string>

#include "shill/service.h"

namespace shill {

class ControlInterface;
class EventDispatcher;
class Manager;
class Metrics;

// This is only use for loading non-active Ethernet service entries from the
// profile.
class EthernetTemporaryService : public Service {
 public:
  EthernetTemporaryService(ControlInterface* control_interface,
                           EventDispatcher* dispatcher,
                           Metrics* metrics,
                           Manager* manager,
                           const std::string& storage_identifier);
  ~EthernetTemporaryService() override;

  // Inherited from Service.
  std::string GetDeviceRpcId(Error* error) const override;
  std::string GetStorageIdentifier() const override;
  bool IsVisible() const override;

 private:
  std::string storage_identifier_;
  DISALLOW_COPY_AND_ASSIGN(EthernetTemporaryService);
};

}  // namespace shill

#endif  // SHILL_ETHERNET_ETHERNET_TEMPORARY_SERVICE_H_
