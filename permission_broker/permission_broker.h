// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERMISSION_BROKER_PERMISSION_BROKER_H_
#define PERMISSION_BROKER_PERMISSION_BROKER_H_

#include <dbus/dbus.h>

#include <string>
#include <vector>

#include <base/macros.h>
#include <base/message_loop/message_loop.h>
#include <base/sequenced_task_runner.h>
#include <chromeos/dbus/exported_object_manager.h>

#include "firewalld/dbus-proxies.h"
#include "permission_broker/dbus_adaptors/org.chromium.PermissionBroker.h"
#include "permission_broker/port_tracker.h"
#include "permission_broker/rule_engine.h"

namespace permission_broker {

// The PermissionBroker encapsulates the execution of a chain of Rules which
// decide whether or not to grant access to a given path. The PermissionBroker
// is also responsible for providing a D-Bus interface to clients.
class PermissionBroker : public org::chromium::PermissionBrokerAdaptor,
                         public org::chromium::PermissionBrokerInterface {
 public:
  PermissionBroker(chromeos::dbus_utils::ExportedObjectManager* object_manager,
                   const std::string& access_group,
                   const std::string& udev_run_path,
                   int poll_interval_msecs);
  ~PermissionBroker();

  // Register the D-Bus object and interfaces.
  void RegisterAsync(
      const chromeos::dbus_utils::AsyncEventSequencer::CompletionAction& cb);

 private:
  // D-Bus methods.
  bool RequestPathAccess(const std::string& in_path,
                         int32_t in_interface_id) override;
  bool RequestTcpPortAccess(uint16_t in_port,
                            const std::string& in_interface,
                            const dbus::FileDescriptor& dbus_fd) override;
  bool RequestUdpPortAccess(uint16_t in_port,
                            const std::string& in_interface,
                            const dbus::FileDescriptor& dbus_fd) override;
  bool ReleaseTcpPort(uint16_t in_port,
                      const std::string& in_interface) override;
  bool ReleaseUdpPort(uint16_t in_port,
                      const std::string& in_interface) override;
  bool RequestVpnSetup(const std::vector<std::string>& usernames,
                       const std::string& interface,
                       const dbus::FileDescriptor& dbus_fd) override;
  bool RemoveVpnSetup() override;

  RuleEngine rule_engine_;
  chromeos::dbus_utils::DBusObject dbus_object_;
  org::chromium::FirewalldProxy firewalld_;
  PortTracker port_tracker_;

  DISALLOW_COPY_AND_ASSIGN(PermissionBroker);
};

}  // namespace permission_broker

#endif  // PERMISSION_BROKER_PERMISSION_BROKER_H_
