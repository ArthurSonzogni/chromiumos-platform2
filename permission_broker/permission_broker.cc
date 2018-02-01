// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "permission_broker/permission_broker.h"

#include <fcntl.h>
#include <linux/usb/ch9.h>
#include <linux/usbdevice_fs.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <brillo/userdb_utils.h>
#include <chromeos/dbus/service_constants.h>

#include "permission_broker/allow_group_tty_device_rule.h"
#include "permission_broker/allow_hidraw_device_rule.h"
#include "permission_broker/allow_tty_device_rule.h"
#include "permission_broker/allow_usb_device_rule.h"
#include "permission_broker/deny_claimed_hidraw_device_rule.h"
#include "permission_broker/deny_claimed_usb_device_rule.h"
#include "permission_broker/deny_group_tty_device_rule.h"
#include "permission_broker/deny_uninitialized_device_rule.h"
#include "permission_broker/deny_unsafe_hidraw_device_rule.h"
#include "permission_broker/deny_usb_device_class_rule.h"
#include "permission_broker/deny_usb_vendor_id_rule.h"
#include "permission_broker/rule.h"

using permission_broker::AllowGroupTtyDeviceRule;
using permission_broker::AllowHidrawDeviceRule;
using permission_broker::AllowTtyDeviceRule;
using permission_broker::AllowUsbDeviceRule;
using permission_broker::DenyClaimedHidrawDeviceRule;
using permission_broker::DenyClaimedUsbDeviceRule;
using permission_broker::DenyGroupTtyDeviceRule;
using permission_broker::DenyUninitializedDeviceRule;
using permission_broker::DenyUnsafeHidrawDeviceRule;
using permission_broker::DenyUsbDeviceClassRule;
using permission_broker::DenyUsbVendorIdRule;
using permission_broker::PermissionBroker;

namespace {
const uint16_t kLinuxFoundationUsbVendorId = 0x1d6b;

const char kErrorDomainPermissionBroker[] = "permission_broker";
const char kPermissionDeniedError[] = "permission_denied";
const char kOpenFailedError[] = "open_failed";
}

namespace permission_broker {

#if USE_CONTAINERS
class JailRequestHandler : public device_jail::DeviceJailServer::Delegate {
 public:
  explicit JailRequestHandler(RuleEngine* rule_engine)
    : rule_engine_(rule_engine) {}

  jail_request_result HandleRequest(const std::string& path) override {
    switch (rule_engine_->ProcessPath(path)) {
    case Rule::ALLOW:
      return JAIL_REQUEST_ALLOW;
    case Rule::ALLOW_WITH_LOCKDOWN:
      return JAIL_REQUEST_ALLOW_WITH_LOCKDOWN;
    case Rule::ALLOW_WITH_DETACH:
      return JAIL_REQUEST_ALLOW_WITH_DETACH;
    default:
      LOG(WARNING) << "Unknown rule engine response";
      // fallthrough
    case Rule::DENY:
      return JAIL_REQUEST_DENY;
    }
  }

 private:
  RuleEngine* rule_engine_;  // weak
};
#endif  // USE_CONTAINERS

PermissionBroker::PermissionBroker(
    brillo::dbus_utils::ExportedObjectManager* object_manager,
    const std::string& access_group_name,
    const std::string& udev_run_path,
    int poll_interval_msecs)
    : org::chromium::PermissionBrokerAdaptor(this),
      rule_engine_(udev_run_path, poll_interval_msecs),
      dbus_object_(object_manager,
                   object_manager->GetBus(),
                   dbus::ObjectPath(kPermissionBrokerServicePath)),
      port_tracker_(&firewall_) {
  CHECK(brillo::userdb::GetGroupInfo(access_group_name, &access_group_))
      << "You must specify a group name via the --access_group flag.";
  rule_engine_.AddRule(new AllowUsbDeviceRule());
  rule_engine_.AddRule(new AllowTtyDeviceRule());
  rule_engine_.AddRule(new DenyClaimedUsbDeviceRule());
  rule_engine_.AddRule(new DenyUninitializedDeviceRule());
  rule_engine_.AddRule(new DenyUsbDeviceClassRule(USB_CLASS_HUB));
  rule_engine_.AddRule(new DenyUsbDeviceClassRule(USB_CLASS_MASS_STORAGE));
  rule_engine_.AddRule(new DenyUsbVendorIdRule(kLinuxFoundationUsbVendorId));
  rule_engine_.AddRule(new AllowHidrawDeviceRule());
  rule_engine_.AddRule(new AllowGroupTtyDeviceRule("serial"));
  rule_engine_.AddRule(new DenyGroupTtyDeviceRule("modem"));
  rule_engine_.AddRule(new DenyGroupTtyDeviceRule("tty"));
  rule_engine_.AddRule(new DenyGroupTtyDeviceRule("uucp"));
  rule_engine_.AddRule(new DenyClaimedHidrawDeviceRule());
  rule_engine_.AddRule(new DenyUnsafeHidrawDeviceRule());

#if USE_CONTAINERS
  // Try to serve device_jail requests. If we can't, it's not a huge deal.
  jail_server_ = device_jail::DeviceJailServer::CreateAndListen(
      std::make_unique<JailRequestHandler>(&rule_engine_),
      base::MessageLoopForIO::current());
  if (!jail_server_)
    LOG(WARNING) << "Jail server failed to start";
#else
  DLOG(INFO) << "Device jail support is turned off";
#endif
}

PermissionBroker::~PermissionBroker() {}

void PermissionBroker::RegisterAsync(
    const brillo::dbus_utils::AsyncEventSequencer::CompletionAction& cb) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(cb);
}

bool PermissionBroker::CheckPathAccess(const std::string& in_path) {
  Rule::Result result = rule_engine_.ProcessPath(in_path);
  return result == Rule::ALLOW || result == Rule::ALLOW_WITH_LOCKDOWN
      || result == Rule::ALLOW_WITH_DETACH;
}

bool PermissionBroker::RequestPathAccess(const std::string& in_path,
                                         int32_t in_interface_id) {
  if (rule_engine_.ProcessPath(in_path) == Rule::ALLOW) {
    return GrantAccess(in_path);
  }
  return false;
}

bool PermissionBroker::OpenPath(brillo::ErrorPtr* error,
                                const std::string& in_path,
                                dbus::FileDescriptor* out_fd) {
  Rule::Result rule_result = rule_engine_.ProcessPath(in_path);
  if (rule_result != Rule::ALLOW && rule_result != Rule::ALLOW_WITH_LOCKDOWN
      && rule_result != Rule::ALLOW_WITH_DETACH) {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, kErrorDomainPermissionBroker, kPermissionDeniedError,
        "Permission to open '%s' denied", in_path.c_str());
    return false;
  }

  int fd = HANDLE_EINTR(open(in_path.c_str(), O_RDWR));
  if (fd < 0) {
    brillo::errors::system::AddSystemError(error, FROM_HERE, errno);
    brillo::Error::AddToPrintf(error, FROM_HERE, kErrorDomainPermissionBroker,
                                 kOpenFailedError, "Failed to open path '%s'",
                                 in_path.c_str());
    return false;
  }

  dbus::FileDescriptor result;
  result.PutValue(fd);
  result.CheckValidity();

  uint32_t mask = -1U;
  if (rule_result == Rule::ALLOW_WITH_LOCKDOWN) {
    if (ioctl(fd, USBDEVFS_DROP_PRIVILEGES, &mask) < 0) {
      brillo::errors::system::AddSystemError(error, FROM_HERE, errno);
      brillo::Error::AddToPrintf(
          error, FROM_HERE, kErrorDomainPermissionBroker, kOpenFailedError,
          "USBDEVFS_DROP_PRIVILEGES ioctl failed on '%s'", in_path.c_str());
      return false;
    }
  }

  if (rule_result == Rule::ALLOW_WITH_DETACH) {
    if (!usb_driver_tracker_.DetachPathFromKernel(fd, in_path))
      return false;
  }

  *out_fd = std::move(result);
  return true;
}

bool PermissionBroker::RequestTcpPortAccess(
    uint16_t in_port,
    const std::string& in_interface,
    const dbus::FileDescriptor& in_lifeline_fd) {
  return port_tracker_.AllowTcpPortAccess(in_port, in_interface,
                                          in_lifeline_fd.value());
}

bool PermissionBroker::RequestUdpPortAccess(
    uint16_t in_port,
    const std::string& in_interface,
    const dbus::FileDescriptor& in_lifeline_fd) {
  return port_tracker_.AllowUdpPortAccess(in_port, in_interface,
                                          in_lifeline_fd.value());
}

bool PermissionBroker::ReleaseTcpPort(uint16_t in_port,
                                      const std::string& in_interface) {
  return port_tracker_.RevokeTcpPortAccess(in_port, in_interface);
}

bool PermissionBroker::ReleaseUdpPort(uint16_t in_port,
                                      const std::string& in_interface) {
  return port_tracker_.RevokeUdpPortAccess(in_port, in_interface);
}

bool PermissionBroker::RequestVpnSetup(
    const std::vector<std::string>& usernames,
    const std::string& interface,
    const dbus::FileDescriptor& in_lifeline_fd) {
  return port_tracker_.PerformVpnSetup(usernames, interface,
                                       in_lifeline_fd.value());
}

bool PermissionBroker::RemoveVpnSetup() {
  return port_tracker_.RemoveVpnSetup();
}

bool PermissionBroker::GrantAccess(const std::string& path) {
  if (chown(path.c_str(), -1, access_group_)) {
    PLOG(INFO) << "Could not grant access to " << path;
    return false;
  }
  return true;
}

}  // namespace permission_broker
