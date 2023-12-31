//
// Copyright (C) 2016 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "update_engine/cros/power_manager_chromeos.h"

#include <memory>

#include <power_manager/dbus-constants.h>
#include <power_manager/dbus-proxies.h>

#include "update_engine/cros/dbus_connection.h"

#include <base/logging.h>

namespace chromeos_update_engine {

namespace power_manager {
std::unique_ptr<PowerManagerInterface> CreatePowerManager() {
  return std::unique_ptr<PowerManagerInterface>(new PowerManagerChromeOS());
}
}  // namespace power_manager

PowerManagerChromeOS::PowerManagerChromeOS()
    : power_manager_proxy_(DBusConnection::Get()->GetDBus()) {}

bool PowerManagerChromeOS::RequestReboot() {
  LOG(INFO) << "Calling " << ::power_manager::kPowerManagerInterface << "."
            << ::power_manager::kRequestRestartMethod;
  brillo::ErrorPtr error;
  return power_manager_proxy_.RequestRestart(
      ::power_manager::REQUEST_RESTART_FOR_UPDATE,
      "update_engine applying update", &error);
}

bool PowerManagerChromeOS::RequestShutdown() {
  LOG(INFO) << "Calling " << ::power_manager::kPowerManagerInterface << "."
            << ::power_manager::kRequestShutdownMethod;
  brillo::ErrorPtr error;
  return power_manager_proxy_.RequestShutdown(
      ::power_manager::REQUEST_SHUTDOWN_FOR_USER,
      "update_engine applying update", &error);
}

}  // namespace chromeos_update_engine
