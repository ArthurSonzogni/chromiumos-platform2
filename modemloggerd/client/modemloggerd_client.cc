// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <base/at_exit.h>
#include <base/task/single_thread_task_executor.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/functional/bind.h>
#include <base/check.h>
#include <base/logging.h>
#include <base/run_loop.h>
#include <brillo/flag_helper.h>
#include <dbus/bus.h>

#include "modemloggerd/dbus_bindings/modemloggerd-proxies.h"
#include "modemloggerd/dbus-constants.h"

void RunAction(const scoped_refptr<dbus::Bus>& bus,
               const dbus::ObjectPath& modem_path,
               const std::string& action) {
  org::chromium::Modemloggerd::ModemProxy modem_proxy(
      bus, modemloggerd::kModemloggerdServiceName, modem_path);
  brillo::ErrorPtr error;
  if (action == "start") {
    modem_proxy.SetEnabled(true, &error);
    CHECK(!error) << error->GetMessage();
    modem_proxy.Start(&error);
    CHECK(!error) << error->GetMessage();
  } else if (action == "stop") {
    modem_proxy.Stop(&error);
    CHECK(!error) << error->GetMessage();
    modem_proxy.SetEnabled(false, &error);
    CHECK(!error) << error->GetMessage();
  } else if (action == "set_auto_start") {
    modem_proxy.SetEnabled(true, &error);
    CHECK(!error) << error->GetMessage();
    modem_proxy.SetAutoStart(true, &error);
    CHECK(!error) << error->GetMessage();
  } else if (action == "clear_auto_start") {
    modem_proxy.SetAutoStart(false, &error);
    CHECK(!error) << error->GetMessage();
  } else {
    LOG(FATAL) << "Invalid action \"" << action << "\"";
  }
  LOG(INFO) << "Success";
  exit(0);
}

void OnPropertiesChanged(
    const scoped_refptr<dbus::Bus>& bus,
    const std::string& action,
    org::chromium::Modemloggerd::ManagerProxyInterface* manager_proxy_interface,
    const std::string& prop) {
  if (prop != manager_proxy_interface->AvailableModemsName()) {
    return;
  }
  CHECK(manager_proxy_interface->is_available_modems_valid());
  if (manager_proxy_interface->available_modems().empty()) {
    LOG(FATAL) << "No logging capable modem found";
  }
  LOG(INFO) << "Found logging capable modem: "
            << manager_proxy_interface->available_modems()[0].value();
  LOG(INFO) << "Default logs directory: /var/log/modemloggerd/";
  RunAction(bus, manager_proxy_interface->available_modems()[0], action);
}

int main(int argc, char* argv[]) {
  DEFINE_string(action, "",
                "logging related action to perform (one of "
                "set_auto_start, clear_auto_start, start, stop)");
  brillo::FlagHelper::Init(argc, argv,
                           "Configures the modem for logging via modemloggerd");

  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);
  base::FileDescriptorWatcher watcher(task_executor.task_runner());

  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus(new dbus::Bus(options));
  CHECK(bus->Connect());

  org::chromium::Modemloggerd::ManagerProxy manager_proxy(
      bus, modemloggerd::kModemloggerdServiceName);
  auto on_props_available_cb = base::BindRepeating(
      &OnPropertiesChanged, std::move(bus), std::move(FLAGS_action));
  manager_proxy.InitializeProperties(std::move(on_props_available_cb));
  base::RunLoop().Run();
  return 0;
}
