// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_ETHERNET_ETHERNET_SERVICE_H_
#define SHILL_ETHERNET_ETHERNET_SERVICE_H_

#include <string>

#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/event_dispatcher.h"
#include "shill/service.h"

namespace shill {

class ControlInterface;
class Ethernet;
class EventDispatcher;
class Manager;
class Metrics;

class EthernetService : public Service {
 public:
  EthernetService(ControlInterface *control_interface,
                  EventDispatcher *dispatcher,
                  Metrics *metrics,
                  Manager *manager,
                  base::WeakPtr<Ethernet> ethernet);
  ~EthernetService() override;

  // Inherited from Service.
  void Connect(Error *error, const char *reason) override;
  void Disconnect(Error *error, const char *reason) override;

  // ethernet_<MAC>
  std::string GetStorageIdentifier() const override;
  bool IsAutoConnectByDefault() const override;
  bool SetAutoConnectFull(const bool &connect, Error *error);

  void Remove(Error *error) override;
  bool IsVisible() const override;
  bool IsAutoConnectable(const char **reason) const override;

  // Called by the Ethernet device when link state has caused the service
  // visibility to change.
  virtual void OnVisibilityChanged();

 protected:
  std::string GetTethering(Error *error) const override;

 private:
  FRIEND_TEST(EthernetServiceTest, GetTethering);

  static const char kAutoConnNoCarrier[];
  static const char kServiceType[];

  std::string GetDeviceRpcId(Error *error) const;

  base::WeakPtr<Ethernet> ethernet_;
  DISALLOW_COPY_AND_ASSIGN(EthernetService);
};

}  // namespace shill

#endif  // SHILL_ETHERNET_ETHERNET_SERVICE_H_
