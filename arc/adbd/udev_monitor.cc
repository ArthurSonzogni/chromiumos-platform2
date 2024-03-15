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

// DbC enable / disable constants
constexpr char kDbcXmlPath[] = "/etc/arc/adbd.json";
constexpr char kEmptyDeviceId[] = "0000:00:00.0";
constexpr char kPciBusIdRegex[] = R"(\"pciBusDeviceId\": \"([^\"]*)\")";
constexpr char kDbcControlPath[] = "/sys/devices/pci0000:00/{PCI_BUS_ID}/dbc";
constexpr char kPciBusIdPlaceholderRegex[] = R"(\{PCI_BUS_ID\})";
constexpr char kDbcEnable[] = "enable";
constexpr char kDbcDisable[] = "disable";

// User space control to modify the USB Type-C role.
// Refer Documentation/ABI/testing/sysfs-class-usb_role.
constexpr char kTypecUsbRoleSysPath[] =
    "/sys/class/typec/port{PORT}/usb-role-switch/role";
constexpr char kUsbRoleSysPath[] =
    "/sys/class/usb_role/CON{PORT}-role-switch/role";
// Regex to replace PORT in USB role setting.
constexpr char kUsbRolePortRegex[] = R"(\{PORT\})";
constexpr char kUsbRoleHost[] = "host";
constexpr char kUsbRoleDevice[] = "device";
}  // namespace

UdevMonitor::UdevMonitor() : udev_thread_("udev_monitor") {}

bool UdevMonitor::Init() {
  num_typec_connections_ = 0;

  // Extract the typec usb pci bus id from adbd.json, eg. 0000:00:0d.0 for brya
  std::string adbd_json = std::string();
  base::FilePath adb_json_path = base::FilePath(kDbcXmlPath);
  if (!base::ReadFileToString(adb_json_path, &adbd_json)) {
    PLOG(ERROR) << "Failed to read " << kDbcXmlPath;
  }
  usb_pci_bus_id_ = std::string(kEmptyDeviceId);
  RE2::PartialMatch(adbd_json, kPciBusIdRegex, &usb_pci_bus_id_);

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
    PLOG(ERROR) << "Failed to start udev thread.";
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
    PLOG(WARNING) << "No existing typec devices.\n";
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

// Enable DbC and update usb role
void UdevMonitor::OnDeviceAdd(const base::FilePath& path) {
  // Every cable event causes multiple adds, only watch for portx-partner
  // events.
  int port_num;
  if (!RE2::FullMatch(path.BaseName().value(), kPartnerRegex, &port_num)) {
    // Not a portx-partner event - ignore
    return;
  }

  // Here we use a simple counting mechanism to enable dbc on the first and
  // only the first portx-partner add event, and then disable it when the last
  // portx-partner connection is removed. This should allow for multiple typec
  // cables to be connected and only disable dbc when none are remaining. If
  // this count gets into a bad state, there is a possibility that we do not
  // correctly disable dbc (unnecessary polling will happen) until
  // reboot or we will not enable dbc until a cable unplug / replug event.
  num_typec_connections_++;
  LOG(INFO) << "Typec connection detected at " << path.BaseName().value()
            << ". Total typec connections: " << num_typec_connections_;

  // Enable DbC if this is the first typec connection
  if (num_typec_connections_ == 1) {
    LOG(INFO) << "First typec cable connected, enabling DbC.";
    UpdateDbcState(kDbcEnable);
  }

  // Update role
  UpdatePortRole(port_num, kUsbRoleHost);
}

// Disable dbc if usb cable unplugged
void UdevMonitor::OnDeviceRemove(const base::FilePath& path) {
  // Only care about portx-partner remove events
  int port_num;
  if (!RE2::FullMatch(path.BaseName().value(), kPartnerRegex, &port_num)) {
    return;
  }

  num_typec_connections_--;
  LOG(INFO) << "Typec connection detected at " << path.BaseName().value()
            << ". Total typec connections: " << num_typec_connections_;

  if (num_typec_connections_ < 0) {
    num_typec_connections_ = 0;
  }

  // Disable DbC if no more typec connections
  // No need to reset the usb mode since no cable is connected
  if (num_typec_connections_ <= 0) {
    LOG(INFO) << "No more typec connections, disabling DbC.";
    UpdateDbcState(kDbcDisable);
  }
}

// Update the DbC control file state with the given state (enable or disable)
void UdevMonitor::UpdateDbcState(const std::string& state) {
  auto dbc_path_string = std::string(kDbcControlPath);
  RE2::Replace(&dbc_path_string, kPciBusIdPlaceholderRegex, usb_pci_bus_id_);
  base::FilePath dbc_control_path = base::FilePath(dbc_path_string);

  if (state != kDbcEnable && state != kDbcDisable) {
    PLOG(ERROR) << "Incorrect DbC state passed to UpdateDbcState. Should be "
                << kDbcEnable << " or " << kDbcDisable << " but " << state
                << " was given.";
    return;
  }

  if (!base::PathExists(dbc_control_path)) {
    PLOG(ERROR) << "DbC control file does not exist";
  } else {
    // Change DbC state
    if (!base::WriteFile(dbc_control_path, state)) {
      PLOG(ERROR) << "Failed to write '" << state << "' to dbc control file";
      return;
    }
    VLOG(1) << "Successfully updated dbc to " << state << " for "
            << dbc_control_path;
  }
}

// Update the USB port's role ("host" or "device")
void UdevMonitor::UpdatePortRole(int port_num, const std::string& role) {
  if (role != kUsbRoleHost && role != kUsbRoleDevice) {
    PLOG(ERROR) << "Incorrect USB role passed to UpdatePortRole. Should be "
                << kUsbRoleHost << " or " << kUsbRoleDevice << " but " << role
                << " was given.";
    return;
  }

  // There are two possible paths to the typec usb role control file
  // 1. /sys/class/typec/port{PORT}/usb-role-switch/role. This should exist
  // in Linux 6.10+
  auto typec_path_string = std::string(kTypecUsbRoleSysPath);
  RE2::Replace(&typec_path_string, kUsbRolePortRegex, std::to_string(port_num));
  base::FilePath typec_usb_role_path = base::FilePath(typec_path_string);

  // 2. /sys/class/usb_role/CON{PORT}-role-switch/role. This should always
  // exist but port_num may not match with CON{port_num}-role-switch so it is
  // used only as a fallback case.
  auto usb_role_path_string = std::string(kUsbRoleSysPath);
  RE2::Replace(&usb_role_path_string, kUsbRolePortRegex,
               std::to_string(port_num));
  base::FilePath usb_role_path = base::FilePath(usb_role_path_string);

  // First check the /sys/class/typec path
  if (base::PathExists(typec_usb_role_path)) {
    // update typec USB role on the /sys/class/typec/portX path
    if (!base::WriteFile(typec_usb_role_path, role)) {
      PLOG(ERROR) << "Failed to write typec usb role to "
                  << typec_usb_role_path;
      return;
    } else {
      VLOG(1) << "Successfully updated usb_role to " << role << " for "
              << typec_usb_role_path;
    }
  } else {
    // Fallback to using /sys/class/usb_role
    if (!base::PathExists(usb_role_path)) {
      PLOG(ERROR) << "Usb role switch control files " << typec_usb_role_path
                  << " and " << usb_role_path << " do not exist";
      return;
    } else {
      // update USB role on the /sys/class/typec path
      if (!base::WriteFile(usb_role_path, role)) {
        PLOG(ERROR) << "Failed to write usb role";
        return;
      } else {
        VLOG(1) << "Successfully updated usb_role to " << role << " for "
                << usb_role_path;
      }
    }
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

  if (action == "remove") {
    UdevMonitor::OnDeviceRemove(path);
  }
}

}  // namespace adbd
