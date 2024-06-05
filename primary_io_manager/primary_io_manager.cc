// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "primary_io_manager/primary_io_manager.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>
#include <libudev.h>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "primary_io_manager/udev.h"
#include "primary_io_manager/udev_scopers.h"

namespace primary_io_manager {

std::string DeviceStateToString(DeviceType type, DeviceState state) {
  if (state == NONE) {
    return "";
  }

  std::string short_type;
  switch (type) {
    case MOUSE:
      short_type = "M";
      break;
    case KEYBOARD:
      short_type = "K";
      break;
  }

  return std::format("{0}{1}", state == PRIMARY ? "*" : "", short_type);
}

std::string IoDeviceToString(std::string syspath, const IoDevice* device) {
  return std::format("{0:>2} {1:>2} {2:>7} {3:>32} {4}",
                     DeviceStateToString(KEYBOARD, device->keyboard),
                     DeviceStateToString(MOUSE, device->mouse),
                     device->busdevnum, device->name, syspath);
}

bool PrimaryIoManager::IsPrimaryIoDevice(const std::string& in_device) {
  PruneDevices();

  auto device_iter = io_devices_.find(in_device);
  if (device_iter != io_devices_.end()) {
    return device_iter->second->keyboard == PRIMARY ||
           device_iter->second->mouse == PRIMARY;
  }

  LOG(INFO) << "Unknown device: " << in_device;
  return false;
}

void PrimaryIoManager::PruneDevices() {
  std::vector<std::string> removable_devices;
  for (auto& [syspath, _] : io_devices_) {
    if (!base::PathExists(base::FilePath(syspath))) {
      removable_devices.push_back(syspath);
    }
  }
  for (auto& path : removable_devices) {
    RemoveDevice(path);
  }
}

void PrimaryIoManager::OnDeviceAdded(const ScopedUdevDevicePtr device) {
  bool is_mouse = false;
  bool is_keyboard = false;

  udev_device* parent = udev_device_get_parent_with_subsystem_devtype(
      device.get(), "usb", "usb_device");
  if (!parent) {
    LOG(WARNING) << "Mouse or keyboard in input subsystem does not have a USB "
                    "parent";
    return;
  }

  const char* syspath = udev_device_get_syspath(parent);
  if (!syspath) {
    LOG(WARNING) << "no syspath for parent device, unable to continue";
    return;
  }

  const char* prop =
      udev_device_get_property_value(device.get(), "ID_INPUT_MOUSE");
  if (prop && !strcmp(prop, "1")) {
    is_mouse = true;
  }
  prop = udev_device_get_property_value(device.get(), "ID_INPUT_KEYBOARD");
  if (prop && !strcmp(prop, "1")) {
    is_keyboard = true;
  }
  prop = udev_device_get_property_value(device.get(), "ID_INPUT_TRACKPAD");
  if (prop && !strcmp(prop, "1")) {
    is_mouse = true;
  }

  if (!is_keyboard && !is_mouse) {
    return;
  }

  // If udev decides to re-enumerate devices we already know about, skip.
  if (io_devices_.contains(syspath)) {
    return;
  }

  const char* name_ptr =
      udev_device_get_property_value(parent, "ID_MODEL_FROM_DATABASE");
  // Prefer name from database when available, but fall back to model name.
  if (!name_ptr || !strcmp(name_ptr, "")) {
    name_ptr = udev_device_get_property_value(parent, "ID_MODEL");
  }
  std::string name = name_ptr ? name_ptr : "";

  std::string busdevnum;
  const char* busnum = udev_device_get_sysattr_value(parent, "busnum");
  const char* devnum = udev_device_get_sysattr_value(parent, "devnum");
  if (busnum && devnum) {
    busdevnum = std::format("{0}:{1}", busnum, devnum);
  }

  if (is_keyboard) {
    AddKeyboard(syspath, name, busdevnum);
  }
  if (is_mouse) {
    AddMouse(syspath, name, busdevnum);
  }
}

void PrimaryIoManager::AddMouse(std::string syspath,
                                std::string name,
                                std::string busdevnum) {
  AddDevice(syspath, DeviceType::MOUSE, name, busdevnum);
}

void PrimaryIoManager::AddKeyboard(std::string syspath,
                                   std::string name,
                                   std::string busdevnum) {
  AddDevice(syspath, DeviceType::KEYBOARD, name, busdevnum);
}

void PrimaryIoManager::AddDevice(std::string syspath,
                                 DeviceType type,
                                 std::string name,
                                 std::string busdevnum) {
  auto device_iter = io_devices_.find(syspath);
  IoDevice* device;

  if (device_iter == io_devices_.end()) {
    std::unique_ptr<IoDevice> new_device = std::make_unique<IoDevice>();
    device = new_device.get();
    device->name = name;
    device->busdevnum = busdevnum;
    io_devices_.insert({syspath, std::move(new_device)});
  } else {
    device = device_iter->second.get();
  }

  switch (type) {
    case primary_io_manager::DeviceType::KEYBOARD:
      if (device->keyboard == NONE) {
        device->keyboard = NONPRIMARY;
      }
      if (!primary_keyboard_) {
        device->keyboard = PRIMARY;
        primary_keyboard_ = device;
      }
      break;
    case primary_io_manager::DeviceType::MOUSE:
      if (device->mouse == NONE) {
        device->mouse = NONPRIMARY;
      }
      if (!primary_mouse_) {
        device->mouse = PRIMARY;
        primary_mouse_ = device;
      }
  }
}

void PrimaryIoManager::RemoveDevice(std::string syspath) {
  auto device_iter = io_devices_.find(syspath);
  if (device_iter == io_devices_.end()) {
    return;
  }

  IoDevice* device = device_iter->second.get();
  bool was_primary_keyboard = (device->keyboard == PRIMARY);
  bool was_primary_mouse = (device->mouse == PRIMARY);

  io_devices_.erase(device_iter);

  if (was_primary_keyboard) {
    primary_keyboard_ = nullptr;
    PickNewPrimary(KEYBOARD);
  }
  if (was_primary_mouse) {
    primary_mouse_ = nullptr;
    PickNewPrimary(MOUSE);
  }
}

void PrimaryIoManager::OnDeviceRemoved(const ScopedUdevDevicePtr device) {
  // At removal time we no longer have information about mouse/keyboard so we
  // have to check all
  udev_device* parent = udev_device_get_parent_with_subsystem_devtype(
      device.get(), "usb", "usb_device");
  if (!parent) {
    return;
  }

  const char* syspath = udev_device_get_syspath(parent);
  if (!syspath) {
    return;
  }

  RemoveDevice(std::string(syspath));
}

void PrimaryIoManager::PickNewPrimary(DeviceType type) {
  for (auto& [_, device] : io_devices_) {
    switch (type) {
      case MOUSE:
        if (device->mouse != NONE) {
          device->mouse = PRIMARY;
          primary_mouse_ = device.get();
          return;
        }
        break;
      case KEYBOARD:
        if (device->keyboard != NONE) {
          device->keyboard = PRIMARY;
          primary_keyboard_ = device.get();
          return;
        }
        break;
    }
  }
}

std::vector<std::string> PrimaryIoManager::GetIoDevices() {
  PruneDevices();

  std::vector<std::string> devices;
  devices.push_back(std::format("{0:>5}|{1:>7}|{2:<32}|{3}", "kb/ms", "bus:dev",
                                "name", "syspath"));
  for (const auto& [path, device] : io_devices_) {
    devices.push_back(IoDeviceToString(path, device.get()));
  }

  return devices;
}

void PrimaryIoManager::UnsetPrimaryKeyboard() {
  if (primary_keyboard_) {
    primary_keyboard_->keyboard = NONPRIMARY;
    primary_keyboard_ = nullptr;
  }
}

void PrimaryIoManager::UnsetPrimaryMouse() {
  if (primary_mouse_) {
    primary_mouse_->mouse = NONPRIMARY;
    primary_mouse_ = nullptr;
  }
}

void PrimaryIoManager::RegisterAsync(
    brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(std::move(cb));
}

PrimaryIoManager::PrimaryIoManager(scoped_refptr<dbus::Bus> bus)
    : primary_io_manager::PrimaryIoManager(bus, UdevImplFactory()) {}

PrimaryIoManager::PrimaryIoManager(scoped_refptr<dbus::Bus> bus,
                                   const UdevFactory& udev_factory)
    : org::chromium::PrimaryIoManagerAdaptor(this),
      dbus_object_(
          nullptr, bus, dbus::ObjectPath(kPrimaryIoManagerServicePath)) {
  udev_ = udev_factory.Create(
      base::BindRepeating(&PrimaryIoManager::OnDeviceAdded,
                          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&PrimaryIoManager::OnDeviceRemoved,
                          weak_factory_.GetWeakPtr()));
  LOG_IF(FATAL, !udev_) << "Failed to create udev wrapper";
}

PrimaryIoManager::~PrimaryIoManager() = default;

}  // namespace primary_io_manager
