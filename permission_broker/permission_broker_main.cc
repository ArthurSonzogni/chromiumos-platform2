// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chromeos/flag_helper.h>
#include <chromeos/syslog_logging.h>

#include "base/command_line.h"
#include "base/logging.h"
#include "chromeos/daemons/dbus_daemon.h"
#include "chromeos/dbus/service_constants.h"
#include "permission_broker/permission_broker.h"

namespace permission_broker {

const char kObjectServicePath[] =
    "/org/chromium/PermissionBroker/ObjectManager";

class Daemon : public chromeos::DBusServiceDaemon {
 public:
  Daemon(std::string access_group, std::string udev_run_path, int poll_interval)
      : DBusServiceDaemon(kPermissionBrokerServiceName,
                          dbus::ObjectPath{kObjectServicePath}),
        access_group_(access_group),
        udev_run_path_(udev_run_path),
        poll_interval_(poll_interval) {}

 protected:
  void RegisterDBusObjectsAsync(
      chromeos::dbus_utils::AsyncEventSequencer* sequencer) override {
    broker_.reset(new PermissionBroker(object_manager_.get(), access_group_,
                                       udev_run_path_, poll_interval_));
    broker_->RegisterAsync(
        sequencer->GetHandler("PermissionBroker.RegisterAsync() failed.",
                              true));
  }

 private:
  std::unique_ptr<PermissionBroker> broker_;
  std::string access_group_;
  std::string udev_run_path_;
  int poll_interval_;

  DISALLOW_COPY_AND_ASSIGN(Daemon);
};

}  // namespace permission_broker

int main(int argc, char** argv) {
  DEFINE_string(access_group, "",
                "The group which has resource access granted to it. "
                "Must not be empty.");
  DEFINE_int32(poll_interval, 100,
               "The interval at which to poll for udev events.");
  DEFINE_string(udev_run_path, "/run/udev",
                "The path to udev's run directory.");

  chromeos::FlagHelper::Init(argc, argv, "Chromium OS Permission Broker");
  chromeos::InitLog(chromeos::kLogToSyslog);

  permission_broker::Daemon daemon(FLAGS_access_group, FLAGS_udev_run_path,
                                   FLAGS_poll_interval);
  return daemon.Run();
}
