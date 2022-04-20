// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "permission_broker/usb_driver_tracker.h"

#include <fcntl.h>
#include <linux/usbdevice_fs.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <utility>

#include <base/bind.h>
#include <base/containers/contains.h>
#include <base/containers/cxx20_erase_vector.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_number_conversions.h>
#include <base/unguessable_token.h>

#include "permission_broker/udev_scopers.h"

namespace permission_broker {

UsbDriverTracker::UsbDriverTracker() = default;

UsbDriverTracker::~UsbDriverTracker() {
  CleanUpTracking();
}

void UsbDriverTracker::HandleClosedFd(std::string client_id) {
  auto iter = dev_fds_.find(client_id);
  if (iter != dev_fds_.end()) {
    auto& entry = iter->second;
    // Reattaching the kernel driver to the USB interface.
    while (!entry.interfaces.empty()) {
      uint8_t iface_num = *entry.interfaces.begin();
      if (!ConnectInterface(entry.fd.get(), iface_num)) {
        LOG(ERROR) << "Failed to reattach interface "
                   << static_cast<int>(iface_num) << " for client "
                   << client_id;
      }
      // This might remove elements in entry.interfaces.
      ClearDetachedInterfaceRecord(client_id, entry.path, iface_num);
    }
    // We are done with the client_id.
    dev_fds_.erase(iter);
  } else {
    LOG(WARNING) << "Untracked USB client " << client_id;
  }
}

bool UsbDriverTracker::DetachPathFromKernel(int fd,
                                            const std::string* client_id,
                                            const base::FilePath& path) {
  // Use the USB device node major/minor to find the udev entry.
  struct stat st;
  if (fstat(fd, &st) || !S_ISCHR(st.st_mode)) {
    LOG(WARNING) << "Cannot stat " << path << " device id";
    return false;
  }

  ScopedUdevPtr udev(udev_new());
  ScopedUdevDevicePtr device(
      udev_device_new_from_devnum(udev.get(), 'c', st.st_rdev));
  if (!device.get()) {
    return false;
  }

  ScopedUdevEnumeratePtr enumerate(udev_enumerate_new(udev.get()));
  udev_enumerate_add_match_parent(enumerate.get(), device.get());
  udev_enumerate_scan_devices(enumerate.get());

  // Try to find our USB interface nodes, by iterating through all devices
  // and extracting our children devices.
  bool detached = false;
  std::vector<uint8_t> ifaces;
  struct udev_list_entry* entry;
  udev_list_entry_foreach(entry,
                          udev_enumerate_get_list_entry(enumerate.get())) {
    const char* entry_path = udev_list_entry_get_name(entry);
    ScopedUdevDevicePtr child(
        udev_device_new_from_syspath(udev.get(), entry_path));

    const char* child_type = udev_device_get_devtype(child.get());
    if (!child_type || strcmp(child_type, "usb_interface") != 0) {
      continue;
    }

    const char* driver = udev_device_get_driver(child.get());
    if (driver) {
      // A kernel driver is using this interface, try to detach it.
      const char* iface =
          udev_device_get_sysattr_value(child.get(), "bInterfaceNumber");
      unsigned iface_num;
      if (!iface || !base::StringToUint(iface, &iface_num)) {
        detached = false;
        continue;
      }

      if (!DisconnectInterface(fd, iface_num)) {
        LOG(WARNING) << "Kernel USB driver disconnection for " << path
                     << " on interface " << iface_num << " failed " << errno;
      } else {
        detached = true;
        ifaces.push_back(iface_num);
        LOG(INFO) << "USB driver '" << driver << "' detached on " << path
                  << " interface " << iface_num;
      }
    }
  }

  if (detached && client_id) {
    for (auto iface_num : ifaces) {
      RecordInterfaceDetached(*client_id, path, iface_num);
    }
  }

  return detached;
}

std::unique_ptr<base::FileDescriptorWatcher::Controller>
UsbDriverTracker::WatchLifelineFd(const std::string& client_id,
                                  int lifeline_fd) {
  return base::FileDescriptorWatcher::WatchReadable(
      lifeline_fd,
      base::BindRepeating(&UsbDriverTracker::HandleClosedFd,
                          weak_ptr_factory_.GetWeakPtr(), client_id));
}

absl::optional<std::string> UsbDriverTracker::RegisterClient(
    int lifeline_fd, const base::FilePath& path) {
  // |dup_lifeline_fd| is the duplicated file descriptor of the client's
  // lifeline pipe read end. The ownership needs to be transferred to the
  // internal tracking structure to keep readable callback registered.
  base::ScopedFD fd(HANDLE_EINTR(open(path.value().c_str(), O_RDWR)));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "Failed to open path " << path;
    return absl::nullopt;
  }
  base::ScopedFD dup_lifeline_fd(HANDLE_EINTR(dup(lifeline_fd)));
  if (!dup_lifeline_fd.is_valid()) {
    PLOG(ERROR) << "Failed to dup lifeline_fd " << lifeline_fd;
    return absl::nullopt;
  }

  std::string client_id;
  do {
    client_id = base::UnguessableToken::Create().ToString();
  } while (base::Contains(dev_fds_, client_id));

  auto controller = WatchLifelineFd(client_id, dup_lifeline_fd.get());
  if (!controller) {
    LOG(ERROR) << "Unable to watch lifeline_fd " << dup_lifeline_fd.get()
               << " for client " << client_id;
    return absl::nullopt;
  }

  dev_fds_.emplace(client_id,
                   UsbInterfaces{.path = path,
                                 .controller = std::move(controller),
                                 .interfaces = {},
                                 .fd = std::move(fd),
                                 .lifeline_fd = std::move(dup_lifeline_fd)});

  return client_id;
}

bool UsbDriverTracker::DisconnectInterface(int fd, uint8_t iface_num) {
  struct usbdevfs_ioctl dio;
  dio.ifno = iface_num;
  dio.ioctl_code = USBDEVFS_DISCONNECT;
  dio.data = nullptr;
  int rc = ioctl(fd, USBDEVFS_IOCTL, &dio);
  if (rc < 0) {
    PLOG(ERROR) << "Failed to disconnect interface "
                << static_cast<int>(iface_num) << " with fd " << fd;
    return false;
  }

  return true;
}

bool UsbDriverTracker::ConnectInterface(int fd, uint8_t iface_num) {
  struct usbdevfs_ioctl dio;
  dio.ifno = iface_num;
  dio.ioctl_code = USBDEVFS_CONNECT;
  dio.data = nullptr;
  int rc = ioctl(fd, USBDEVFS_IOCTL, &dio);
  if (rc < 0) {
    PLOG(ERROR) << "Failed to connect interface " << static_cast<int>(iface_num)
                << " with fd " << fd;
    return false;
  }

  return true;
}

void UsbDriverTracker::RecordInterfaceDetached(const std::string& client_id,
                                               const base::FilePath& path,
                                               uint8_t iface_num) {
  auto client_it = dev_fds_.find(client_id);
  DCHECK(client_it != dev_fds_.end());

  DCHECK(!base::Contains(client_it->second.interfaces, iface_num));
  client_it->second.interfaces.push_back(iface_num);
  dev_ifaces_[path][iface_num] = client_id;
}

void UsbDriverTracker::ClearDetachedInterfaceRecord(
    const std::string& client_id,
    const base::FilePath& path,
    uint8_t iface_num) {
  auto client_it = dev_fds_.find(client_id);
  auto path_it = dev_ifaces_.find(path);
  DCHECK(client_it != dev_fds_.end());
  DCHECK(path_it != dev_ifaces_.end());

  auto num_erased = base::Erase(client_it->second.interfaces, iface_num);
  DCHECK_EQ(num_erased, 1);
  path_it->second.erase(iface_num);
  if (path_it->second.empty()) {
    dev_ifaces_.erase(path_it);
  }
}

bool UsbDriverTracker::IsClientIdTracked(const std::string& client_id) {
  return base::Contains(dev_fds_, client_id);
}

void UsbDriverTracker::CleanUpTracking() {
  // Reattach all delegated USB interfaces.
  while (!dev_fds_.empty()) {
    // This might remove the element.
    HandleClosedFd(dev_fds_.begin()->first);
  }
}

}  // namespace permission_broker
