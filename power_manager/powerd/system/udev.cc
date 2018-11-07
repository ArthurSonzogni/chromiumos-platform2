// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/udev.h"

#include <libudev.h>
#include <linux/input.h>

#include <utility>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/free_deleter.h>
#include <base/strings/string_number_conversions.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/system/tagged_device.h"
#include "power_manager/powerd/system/udev_subsystem_observer.h"
#include "power_manager/powerd/system/udev_tagged_device_observer.h"

namespace power_manager {
namespace system {

namespace {

const char kBluetoothSysfsPath[] = "/sys/class/bluetooth/hci0";
const char kFingerprintSysfsPath[] = "/sys/class/chromeos/cros_fp";
const char kPowerdRoleCrosFP[] = "cros_fingerprint";
const char kPowerdRoleVar[] = "POWERD_ROLE";
const char kPowerdUdevTag[] = "powerd";
const char kPowerdTagsVar[] = "POWERD_TAGS";
// Udev device type for USB devices.
const char kUSBDevice[] = "usb_device";

bool HasPowerdRole(struct udev_device* device, const std::string& role) {
  const char* role_cstr =
      udev_device_get_property_value(device, kPowerdRoleVar);
  const std::string device_role = role_cstr ? role_cstr : "";
  return role == device_role;
}

UdevEvent::Action StrToAction(const char* action_str) {
  if (!action_str)
    return UdevEvent::Action::UNKNOWN;
  else if (strcmp(action_str, "add") == 0)
    return UdevEvent::Action::ADD;
  else if (strcmp(action_str, "remove") == 0)
    return UdevEvent::Action::REMOVE;
  else if (strcmp(action_str, "change") == 0)
    return UdevEvent::Action::CHANGE;
  else if (strcmp(action_str, "online") == 0)
    return UdevEvent::Action::ONLINE;
  else if (strcmp(action_str, "offline") == 0)
    return UdevEvent::Action::OFFLINE;
  else
    return UdevEvent::Action::UNKNOWN;
}

bool IsFingerprintDevice(struct udev_device* device) {
  if (HasPowerdRole(device, kPowerdRoleCrosFP))
    return true;

  // We assign powerd roles to the input device. In case |syspath| points to
  // a event device, look also at the parent device to see if it has
  // |kPowerdRoleCrosFP| role.
  struct udev_device* parent = udev_device_get_parent(device);
  return parent && HasPowerdRole(parent, kPowerdRoleCrosFP);
}

bool IsBluetoothDevice(struct udev_device* device) {
  const char* num_str = udev_device_get_sysattr_value(device, "id/bustype");
  int num = 0;

  if (num_str && base::StringToInt(num_str, &num) && num == BUS_BLUETOOTH)
    return true;

  // Also check parent device because event device doesn't expose |id/bustype|
  // attribute and this breaks logic in input_watcher.cc to detect wake sources.
  struct udev_device* parent = udev_device_get_parent(device);
  return parent && IsBluetoothDevice(parent);
}

base::FilePath ResolvePathSymlink(const base::FilePath& link_path) {
  if (!base::IsLink(link_path))
    return link_path;

  base::FilePath actual_path;
  if (!base::ReadSymbolicLink(link_path, &actual_path)) {
    PLOG(ERROR) << "Failed to read symlink " << link_path.value();
    return base::FilePath();
  }

  return actual_path.IsAbsolute() ? actual_path
                                  : link_path.DirName().Append(actual_path);
}

struct UdevDeviceDeleter {
  void operator()(udev_device* dev) {
    if (dev)
      udev_device_unref(dev);
  }
};

}  // namespace

Udev::Udev() : udev_(NULL), udev_monitor_(NULL), watcher_(FROM_HERE) {}

Udev::~Udev() {
  if (udev_monitor_)
    udev_monitor_unref(udev_monitor_);
  if (udev_)
    udev_unref(udev_);
}

bool Udev::Init() {
  udev_ = udev_new();
  if (!udev_) {
    PLOG(ERROR) << "udev_new() failed";
    return false;
  }

  udev_monitor_ = udev_monitor_new_from_netlink(udev_, "udev");
  if (!udev_monitor_) {
    PLOG(ERROR) << "udev_monitor_new_from_netlink() failed";
    return false;
  }

  if (udev_monitor_filter_add_match_tag(udev_monitor_, kPowerdUdevTag))
    LOG(ERROR) << "udev_monitor_filter_add_match_tag failed";
  if (udev_monitor_filter_update(udev_monitor_))
    LOG(ERROR) << "udev_monitor_filter_update failed";

  udev_monitor_enable_receiving(udev_monitor_);

  int fd = udev_monitor_get_fd(udev_monitor_);
  if (!base::MessageLoopForIO::current()->WatchFileDescriptor(
          fd, true, base::MessageLoopForIO::WATCH_READ, &watcher_, this)) {
    LOG(ERROR) << "Unable to watch FD " << fd;
    return false;
  }

  LOG(INFO) << "Watching FD " << fd << " for udev events";

  EnumerateTaggedDevices();

  return true;
}

void Udev::AddSubsystemObserver(const std::string& subsystem,
                                UdevSubsystemObserver* observer) {
  DCHECK(udev_) << "Object uninitialized";
  DCHECK(observer);
  auto it = subsystem_observers_.find(subsystem);
  if (it == subsystem_observers_.end()) {
    it = subsystem_observers_
             .emplace(
                 subsystem,
                 std::make_unique<base::ObserverList<UdevSubsystemObserver>>())
             .first;
  }
  it->second->AddObserver(observer);
}

void Udev::RemoveSubsystemObserver(const std::string& subsystem,
                                   UdevSubsystemObserver* observer) {
  DCHECK(observer);
  auto it = subsystem_observers_.find(subsystem);
  if (it != subsystem_observers_.end())
    it->second->RemoveObserver(observer);
}

void Udev::AddTaggedDeviceObserver(UdevTaggedDeviceObserver* observer) {
  tagged_device_observers_.AddObserver(observer);
}

void Udev::RemoveTaggedDeviceObserver(UdevTaggedDeviceObserver* observer) {
  tagged_device_observers_.RemoveObserver(observer);
}

std::vector<TaggedDevice> Udev::GetTaggedDevices() {
  std::vector<TaggedDevice> devices;
  devices.reserve(tagged_devices_.size());
  for (const std::pair<std::string, TaggedDevice>& pair : tagged_devices_)
    devices.push_back(pair.second);
  return devices;
}

bool Udev::GetSubsystemDevices(const std::string& subsystem,
                               std::vector<UdevDeviceInfo>* devices_out) {
  DCHECK(udev_);
  DCHECK(devices_out);
  struct udev_enumerate* enumerate = udev_enumerate_new(udev_);
  if (!enumerate) {
    LOG(ERROR) << "udev_enumerate_new failed";
    return false;
  }
  int ret = udev_enumerate_add_match_subsystem(enumerate, subsystem.c_str());
  if (ret != 0) {
    LOG(ERROR) << "udev_enumerate_add_match_subsystem failed. Error: "
               << strerror(-ret);
    udev_enumerate_unref(enumerate);
    return false;
  }
  ret = udev_enumerate_scan_devices(enumerate);
  if (ret != 0) {
    LOG(ERROR) << "udev_enumerate_scan_devices failed. Error: "
               << strerror(-ret);
    udev_enumerate_unref(enumerate);
    return false;
  }

  devices_out->clear();

  for (struct udev_list_entry* list_entry =
           udev_enumerate_get_list_entry(enumerate);
       list_entry != NULL; list_entry = udev_list_entry_get_next(list_entry)) {
    const char* syspath = udev_list_entry_get_name(list_entry);
    struct udev_device* device = udev_device_new_from_syspath(udev_, syspath);
    if (!device) {
      LOG(ERROR) << "Enumeration of device with syspath " << syspath
                 << " failed";
      continue;
    }
    UdevDeviceInfo device_info;
    if (GetDeviceInfo(device, &device_info)) {
      devices_out->push_back(std::move(device_info));
    } else {
      LOG(ERROR) << "Could not retrieve Udev info for the device with syspath "
                 << syspath;
    }
    udev_device_unref(device);
  }

  udev_enumerate_unref(enumerate);
  return true;
}

bool Udev::GetSysattr(const std::string& syspath,
                      const std::string& sysattr,
                      std::string* value) {
  DCHECK(udev_);
  DCHECK(value);
  value->clear();

  struct udev_device* device =
      udev_device_new_from_syspath(udev_, syspath.c_str());
  if (!device) {
    LOG(WARNING) << "Failed to open udev device: " << syspath;
    return false;
  }
  const char* value_cstr =
      udev_device_get_sysattr_value(device, sysattr.c_str());
  if (value_cstr)
    *value = value_cstr;
  udev_device_unref(device);
  return value_cstr != NULL;
}

bool Udev::SetSysattr(const std::string& syspath,
                      const std::string& sysattr,
                      const std::string& value) {
  DCHECK(udev_);

  struct udev_device* device =
      udev_device_new_from_syspath(udev_, syspath.c_str());
  if (!device) {
    LOG(WARNING) << "Failed to open udev device: " << syspath;
    return false;
  }
  // udev can modify this value, hence we copy it first.
  std::unique_ptr<char, base::FreeDeleter> value_mutable(strdup(value.c_str()));
  int rv = udev_device_set_sysattr_value(device, sysattr.c_str(),
                                         value_mutable.get());
  udev_device_unref(device);
  if (rv != 0) {
    LOG(WARNING) << "Failed to set sysattr '" << sysattr << "' on device "
                 << syspath << ": " << strerror(-rv);
    return false;
  }
  return true;
}

base::FilePath Udev::FindParentWithSysattr(const std::string& syspath,
                                           const std::string& sysattr,
                                           const std::string& stop_at_devtype) {
  DCHECK(udev_);

  struct udev_device* device =
      udev_device_new_from_syspath(udev_, syspath.c_str());
  if (!device) {
    LOG(WARNING) << "Failed to open udev device: " << syspath;
    return base::FilePath();
  }

  struct udev_device* parent = device;
  while (parent) {
    const char* value = udev_device_get_sysattr_value(parent, sysattr.c_str());
    const char* devtype = udev_device_get_devtype(parent);
    if (value)
      break;
    // Go up one level unless the devtype matches stop_at_devtype.
    if (devtype && strcmp(stop_at_devtype.c_str(), devtype) == 0) {
      parent = nullptr;
    } else {
      // Returns a pointer to the parent device. No additional reference to
      // the device is acquired, but the child device owns a reference to the
      // parent device.
      parent = udev_device_get_parent(parent);
    }
  }
  base::FilePath parent_syspath;
  if (parent)
    parent_syspath = base::FilePath(udev_device_get_syspath(parent));
  udev_device_unref(device);
  return parent_syspath;
}

base::FilePath Udev::FindWakeCapableParent(const std::string& syspath) {
  struct udev_device* device =
      udev_device_new_from_syspath(udev_, syspath.c_str());
  if (!device)
    return base::FilePath();

  // Special cases when input device doesn't have parent wake capable device.
  std::string path_with_wake_parent;
  if (IsBluetoothDevice(device)) {
    path_with_wake_parent =
        ResolvePathSymlink(base::FilePath(kBluetoothSysfsPath)).value();
  } else if (IsFingerprintDevice(device)) {
    path_with_wake_parent =
        ResolvePathSymlink(base::FilePath(kFingerprintSysfsPath)).value();
  } else {
    path_with_wake_parent = syspath;
  }

  udev_device_unref(device);
  return FindParentWithSysattr(path_with_wake_parent, kPowerWakeup, kUSBDevice);
}

bool Udev::GetDeviceInfo(struct udev_device* dev,
                         UdevDeviceInfo* device_info_out) {
  DCHECK(device_info_out);

  const char* subsystem = udev_device_get_subsystem(dev);
  if (!subsystem)
    return false;

  device_info_out->subsystem = subsystem;

  const char* devtype = udev_device_get_devtype(dev);
  if (devtype)
    device_info_out->devtype = devtype;

  const char* sysname = udev_device_get_sysname(dev);
  if (sysname)
    device_info_out->sysname = sysname;

  const char* syspath = udev_device_get_syspath(dev);
  if (syspath)
    device_info_out->syspath = syspath;

  device_info_out->wakeup_device_path = FindWakeCapableParent(syspath);

  return true;
}

bool Udev::GetDevlinks(const std::string& syspath,
                       std::vector<std::string>* out) {
  DCHECK(udev_);
  DCHECK(out);

  std::unique_ptr<udev_device, UdevDeviceDeleter> device(
      udev_device_new_from_syspath(udev_, syspath.c_str()));
  if (!device) {
    PLOG(WARNING) << "Failed to open udev device: " << syspath;
    return false;
  }

  out->clear();

  // TODO(egranata): maybe write a wrapper around udev_list to support
  // for(entry : list) {...}
  struct udev_list_entry* devlink =
      udev_device_get_devlinks_list_entry(device.get());
  while (devlink) {
    const char* name = udev_list_entry_get_name(devlink);
    if (name)
      out->push_back(name);
    devlink = udev_list_entry_get_next(devlink);
  }

  return true;
}

void Udev::OnFileCanReadWithoutBlocking(int fd) {
  struct udev_device* dev = udev_monitor_receive_device(udev_monitor_);
  if (!dev)
    return;

  const char* subsystem = udev_device_get_subsystem(dev);
  const char* sysname = udev_device_get_sysname(dev);
  const char* action_str = udev_device_get_action(dev);
  UdevEvent::Action action = StrToAction(action_str);

  VLOG(1) << "Received event: subsystem=" << subsystem << " sysname=" << sysname
          << " action=" << action_str;

  HandleSubsystemEvent(action, dev);
  HandleTaggedDevice(action, dev);

  udev_device_unref(dev);
}

void Udev::OnFileCanWriteWithoutBlocking(int fd) {
  NOTREACHED() << "Unexpected non-blocking write notification for FD " << fd;
}

void Udev::HandleSubsystemEvent(UdevEvent::Action action,
                                struct udev_device* dev) {
  UdevEvent event;
  if (!GetDeviceInfo(dev, &(event.device_info)))
    return;
  event.action = action;
  auto it = subsystem_observers_.find(event.device_info.subsystem);
  if (it != subsystem_observers_.end()) {
    for (UdevSubsystemObserver& observer : *it->second)
      observer.OnUdevEvent(event);
  }
}

void Udev::HandleTaggedDevice(UdevEvent::Action action,
                              struct udev_device* dev) {
  if (!udev_device_has_tag(dev, kPowerdUdevTag))
    return;

  const char* syspath = udev_device_get_syspath(dev);
  const char* tags = udev_device_get_property_value(dev, kPowerdTagsVar);

  switch (action) {
    case UdevEvent::Action::ADD:
    case UdevEvent::Action::CHANGE:
      TaggedDeviceChanged(syspath, FindWakeCapableParent(syspath),
                          tags ? tags : "");
      break;

    case UdevEvent::Action::REMOVE:
      TaggedDeviceRemoved(syspath);
      break;

    default:
      break;
  }
}

void Udev::TaggedDeviceChanged(const std::string& syspath,
                               const base::FilePath& wakeup_device_path,
                               const std::string& tags) {
  if (!tags.empty()) {
    LOG(INFO) << (tagged_devices_.count(syspath) ? "Updating" : "Adding")
              << " device " << syspath << " with tags " << tags;
  }

  // Replace existing device with same syspath.
  tagged_devices_[syspath] = TaggedDevice(syspath, wakeup_device_path, tags);
  const TaggedDevice& device = tagged_devices_[syspath];
  for (UdevTaggedDeviceObserver& observer : tagged_device_observers_)
    observer.OnTaggedDeviceChanged(device);
}

void Udev::TaggedDeviceRemoved(const std::string& syspath) {
  TaggedDevice device = tagged_devices_[syspath];
  if (!device.tags().empty())
    LOG(INFO) << "Removing device " << syspath;
  tagged_devices_.erase(syspath);
  for (UdevTaggedDeviceObserver& observer : tagged_device_observers_)
    observer.OnTaggedDeviceRemoved(device);
}

bool Udev::EnumerateTaggedDevices() {
  DCHECK(udev_);

  struct udev_enumerate* enumerate = udev_enumerate_new(udev_);
  if (!enumerate) {
    LOG(ERROR) << "udev_enumerate_new failed";
    return false;
  }
  if (udev_enumerate_add_match_tag(enumerate, kPowerdUdevTag) != 0) {
    LOG(ERROR) << "udev_enumerate_add_match_tag failed";
    udev_enumerate_unref(enumerate);
    return false;
  }
  if (udev_enumerate_scan_devices(enumerate) != 0) {
    LOG(ERROR) << "udev_enumerate_scan_devices failed";
    udev_enumerate_unref(enumerate);
    return false;
  }

  tagged_devices_.clear();

  struct udev_list_entry* entry = NULL;
  udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(enumerate)) {
    const char* syspath = udev_list_entry_get_name(entry);
    struct udev_device* device = udev_device_new_from_syspath(udev_, syspath);
    if (!device) {
      LOG(ERROR) << "Enumerated device does not exist: " << syspath;
      continue;
    }
    const char* tags_cstr =
        udev_device_get_property_value(device, kPowerdTagsVar);
    const std::string tags = tags_cstr ? tags_cstr : "";
    if (!tags.empty())
      LOG(INFO) << "Adding device " << syspath << " with tags " << tags;
    tagged_devices_[syspath] =
        TaggedDevice(syspath, FindWakeCapableParent(syspath), tags);
    udev_device_unref(device);
  }
  udev_enumerate_unref(enumerate);
  return true;
}

}  // namespace system
}  // namespace power_manager
