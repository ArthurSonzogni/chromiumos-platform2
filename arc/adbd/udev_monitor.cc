// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/udev/udev_enumerate.h>
#include <re2/re2.h>

#include "arc/adbd/udev_monitor.h"

namespace adbd {

namespace {

constexpr char kUdev[] = "udev";
constexpr char kTypeCSubsystem[] = "typec";
// Regex to detect typec port partner events.
constexpr char kPartnerRegex[] = R"(port(\d+)-partner)";
// User space control to modify the USB Type-C role.
// Refer Documentation/ABI/testing/sysfs-class-usb_role.
constexpr char kUsbRoleSysPath[] =
    "/sys/class/usb_role/CON{PORT}-role-switch/role";
// Regex to replace PORT in USB role setting.
constexpr char kUsbRolePortRegex[] = R"(\{PORT\})";
constexpr char kUsbRole[] = "host";
}  // namespace

UdevMonitor::UdevMonitor() : udev_thread_("udev_monitor") {}

bool UdevMonitor::Init() {
  udev_ = brillo::Udev::Create();

  if (!udev_) {
    PLOG(ERROR) << "Failed to initialize udev object.";
    return false;
  }

  // Enumerate existing devices and update usb role.
  if (!UdevMonitor::Enumerate()) {
    PLOG(ERROR) << "Failed to enumerate existing devices.";
    return false;
  }

  // Set up udev monitor for typec usb events.
  udev_monitor_ = udev_->CreateMonitorFromNetlink(kUdev);
  if (!udev_monitor_) {
    PLOG(ERROR) << "Failed to create udev monitor.";
    return false;
  }

  if (!udev_monitor_->FilterAddMatchSubsystemDeviceType(kTypeCSubsystem,
                                                        nullptr)) {
    PLOG(ERROR) << "Failed to add typec subsystem to udev monitor.";
    return false;
  }

  if (!udev_monitor_->FilterAddMatchSubsystemDeviceType("tty", nullptr)) {
    PLOG(ERROR) << "Failed to add typec subsystem to udev monitor.";
    return false;
  }

  if (!udev_monitor_->EnableReceiving()) {
    PLOG(ERROR) << "Failed to enable receiving for udev monitor.";
    return false;
  }

  int fd = udev_monitor_->GetFileDescriptor();
  if (fd == brillo::UdevMonitor::kInvalidFileDescriptor) {
    PLOG(ERROR) << "Failed to get udev monitor fd.";
    return false;
  }

  if (!udev_thread_.StartWithOptions(
          base::Thread::Options(base::MessagePumpType::IO, 0))) {
    LOG(ERROR) << "Failed to start udev thread.";
    return -1;
  }

  udev_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&UdevMonitor::StartWatching, base::Unretained(this), fd));
  VLOG(1) << "Udev monitor started";
  return true;
}

bool UdevMonitor::Enumerate() {
  DCHECK(udev_);

  auto enumerate = udev_->CreateEnumerate();
  if (!enumerate->AddMatchSubsystem(kTypeCSubsystem)) {
    PLOG(ERROR) << "Failed to add typec subsystem to udev enumerate.";
    return false;
  }

  enumerate->ScanDevices();

  auto entry = enumerate->GetListEntry();
  if (!entry) {
    LOG(WARNING) << "No existing typec devices.\n";
    return true;
  }

  while (entry != nullptr) {
    UdevMonitor::OnDeviceAdd(base::FilePath(std::string(entry->GetName())));
    entry = entry->GetNext();
  }

  return true;
}

void UdevMonitor::StartWatching(int fd) {
  udev_monitor_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      fd,
      base::BindRepeating(&UdevMonitor::OnUdevEvent, base::Unretained(this)));
  if (!udev_monitor_watcher_) {
    PLOG(ERROR) << "Failed to start watcher for udev monitor fd.";
  }
}

// Update usb role for each newly added typec port
void UdevMonitor::OnDeviceAdd(const base::FilePath& path) {
  int port_num;
  if (RE2::FullMatch(path.BaseName().value(), kPartnerRegex, &port_num)) {
    auto path = std::string(kUsbRoleSysPath);
    RE2::Replace(&path, kUsbRolePortRegex, std::to_string(port_num));
    base::FilePath usb_role_path = base::FilePath(path);

    if (!base::PathExists(usb_role_path)) {
      PLOG(ERROR) << "Usb role switch control file does not exist";
      return;
    }

    // update USB role to "host"
    if (!base::WriteFile(usb_role_path, kUsbRole)) {
      PLOG(ERROR) << "Failed to write usb role";
      return;
    }
    VLOG(1) << "Successfully updated usb_role to " << kUsbRole << " for "
            << usb_role_path;
  }
}

// Callback for subscribed udev events.
void UdevMonitor::OnUdevEvent() {
  auto device = udev_monitor_->ReceiveDevice();
  if (!device) {
    LOG(ERROR) << "Udev receive device failed.";
    return;
  }

  auto path = base::FilePath(device->GetSysPath());
  if (path.empty()) {
    LOG(ERROR) << "Failed to get device syspath.";
    return;
  }

  auto action = std::string(device->GetAction());
  if (action.empty()) {
    LOG(ERROR) << "Failed to get device action.";
    return;
  }

  auto subsystem = std::string(device->GetSubsystem());
  if (subsystem.empty()) {
    LOG(ERROR) << "Failed to get device subsystem";
    return;
  }

  if (action == "add") {
    UdevMonitor::OnDeviceAdd(path);
  }
}

}  // namespace adbd
