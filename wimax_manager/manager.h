// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WIMAX_MANAGER_MANAGER_H_
#define WIMAX_MANAGER_MANAGER_H_

#include <memory>
#include <vector>

#include <base/files/file_path.h>
#include <base/macros.h>
#include <base/timer/timer.h>
#include <gtest/gtest_prod.h>

#include "wimax_manager/dbus_adaptable.h"
#include "wimax_manager/dbus_service.h"
#include "wimax_manager/network.h"

namespace wimax_manager {

class Config;
class Device;
class Driver;
class EventDispatcher;
class ManagerDBusAdaptor;
class NetworkOperator;

class Manager : public DBusAdaptable<Manager, ManagerDBusAdaptor> {
 public:
  explicit Manager(EventDispatcher *dispatcher);
  virtual ~Manager();

  bool Initialize();
  bool Finalize();
  bool ScanDevices();
  void OnDeviceScan();
  void CancelDeviceScan();

  void Suspend();
  void Resume();

  const NetworkOperator *GetNetworkOperator(
      Network::Identifier network_id) const;

  const std::vector<std::unique_ptr<Device>> &devices() const {
    return devices_;
  }

 private:
  FRIEND_TEST(ManagerTest, GetNetworkOperator);
  FRIEND_TEST(ManagerTest, LoadEmptyConfigFile);
  FRIEND_TEST(ManagerTest, LoadInvalidConfigFile);
  FRIEND_TEST(ManagerTest, LoadNonExistentConfigFile);
  FRIEND_TEST(ManagerTest, LoadValidConfigFile);

  bool LoadConfig(const base::FilePath &config_file);

  EventDispatcher *dispatcher() const { return dispatcher_; }

 private:
  EventDispatcher *dispatcher_;
  std::unique_ptr<Config> config_;
  std::unique_ptr<Driver> driver_;
  std::vector<std::unique_ptr<Device>> devices_;

  int num_device_scans_;
  base::OneShotTimer device_scan_timer_;
  DBusService dbus_service_;

  DISALLOW_COPY_AND_ASSIGN(Manager);
};

}  // namespace wimax_manager

#endif  // WIMAX_MANAGER_MANAGER_H_
