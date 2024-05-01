// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRIMARY_IO_MANAGER_PRIMARY_IO_MANAGER_H_
#define PRIMARY_IO_MANAGER_PRIMARY_IO_MANAGER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <dbus/dbus.h>
#include <base/time/time.h>
#include <gtest/gtest_prod.h>

#include "base/memory/weak_ptr.h"
#include "primary_io_manager/dbus_adaptors/org.chromium.PrimaryIoManager.h"
#include "primary_io_manager/udev.h"
#include "primary_io_manager/udev_scopers.h"

namespace primary_io_manager {

enum DeviceType { MOUSE, KEYBOARD };

enum DeviceState { PRIMARY, NONPRIMARY, NONE };

struct IoDevice {
  // Though generally unexpected, it is not impossible for a device to present
  // as both a keyboard and mouse. Therefore we must also allow it to be both
  // primary keyboard and mouse.
  DeviceState mouse = NONE;
  DeviceState keyboard = NONE;
  std::string name;
  // TODO(drmasquatch) add time added, for iteration order?
};

// PrimaryIoManager is responsible for keeping track of 'primary' keyboards and
// mice on chromebox form-factor devices, to prevent them from automatically
// being available for pass through to guests.
class PrimaryIoManager : public org::chromium::PrimaryIoManagerAdaptor,
                         public org::chromium::PrimaryIoManagerInterface {
 public:
  PrimaryIoManager(scoped_refptr<dbus::Bus> bus,
                   const UdevFactory& udev_factory);
  explicit PrimaryIoManager(scoped_refptr<dbus::Bus> bus);
  PrimaryIoManager(const PrimaryIoManager&) = delete;
  PrimaryIoManager& operator=(const PrimaryIoManager&) = delete;

  ~PrimaryIoManager();

  // Register the D-Bus object and interfaces.
  void RegisterAsync(
      brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb);

  void AddDevice(std::string syspath, DeviceType type, std::string name);
  void AddKeyboard(std::string syspath, std::string name);
  void AddMouse(std::string syspath, std::string name);
  void PickNewPrimary(DeviceType type);
  void RemoveDevice(std::string syspath);

 private:
  // D-Bus methods.
  std::vector<std::string> GetIoDevices() override;
  void UnsetPrimaryKeyboard() override;
  void UnsetPrimaryMouse() override;
  bool IsPrimaryIoDevice(const std::string& in_device) override;

  void OnDeviceAdded(const ScopedUdevDevicePtr);
  void OnDeviceRemoved(const ScopedUdevDevicePtr);

  // Before returning device list or checking for primary-ness, run through
  // devices we are keeping track of and make sure they all still exist.
  void PruneDevices();

  std::unique_ptr<Udev> udev_;
  brillo::dbus_utils::DBusObject dbus_object_;

  // Tracked devices.
  std::map<std::string, std::unique_ptr<IoDevice>> io_devices_;
  IoDevice* primary_mouse_ = nullptr;
  IoDevice* primary_keyboard_ = nullptr;

  base::WeakPtrFactory<PrimaryIoManager> weak_factory_{this};

  // Friend tests
  FRIEND_TEST(PrimaryIoManagerTest, EmptyManager);
  FRIEND_TEST(PrimaryIoManagerTest, AddMouse);
  FRIEND_TEST(PrimaryIoManagerTest, AddKeyboard);
  FRIEND_TEST(PrimaryIoManagerTest, AddKeyboardAndMouse);
  FRIEND_TEST(PrimaryIoManagerTest, AddKeyboardAndMouseSameDevice);
  FRIEND_TEST(PrimaryIoManagerTest, AddAndRemoveDevices);
  FRIEND_TEST(PrimaryIoManagerTest, TwoMice_RemovePrimary);
  FRIEND_TEST(PrimaryIoManagerTest, TwoMice_RemoveSecondary);
};

}  // namespace primary_io_manager

#endif  // PRIMARY_IO_MANAGER_PRIMARY_IO_MANAGER_H_
