// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/cros/connection_manager.h"

#include <memory>
#include <set>
#include <string>

#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/string_util.h>
#include <policy/device_policy.h>
#include <shill/dbus-constants.h>
#include <shill/dbus-proxies.h>

#include "update_engine/common/connection_utils.h"
#include "update_engine/common/prefs.h"
#include "update_engine/common/system_state.h"
#include "update_engine/common/utils.h"
#include "update_engine/cros/shill_proxy.h"
#include "update_engine/cros/update_attempter.h"

using org::chromium::flimflam::ManagerProxyInterface;
using org::chromium::flimflam::ServiceProxyInterface;
using std::set;
using std::string;

namespace chromeos_update_engine {

namespace connection_manager {
std::unique_ptr<ConnectionManagerInterface> CreateConnectionManager() {
  return std::unique_ptr<ConnectionManagerInterface>(
      new ConnectionManager(new ShillProxy()));
}
}  // namespace connection_manager

ConnectionManager::ConnectionManager(ShillProxyInterface* shill_proxy)
    : shill_proxy_(shill_proxy) {}

bool ConnectionManager::IsUpdateAllowedOverMetered() const {
  const policy::DevicePolicy* device_policy =
      SystemState::Get()->device_policy();

  // The device_policy is loaded in a lazy way before an update check. Load
  // it now from the libbrillo cache if it wasn't already loaded.
  if (!device_policy) {
    UpdateAttempter* update_attempter = SystemState::Get()->update_attempter();
    if (update_attempter) {
      update_attempter->RefreshDevicePolicy();
      device_policy = SystemState::Get()->device_policy();
    }
  }

  if (!device_policy) {
    // Device policy fails to be loaded (possibly due to guest account). We
    // do not check the local user setting here, which should be checked by
    // |OmahaRequestAction| during checking for update.
    LOG(INFO) << "Allowing updates over metered network as device policy fails "
                 "to be loaded.";
    return true;
  }

  set<string> allowed_types;
  if (device_policy->GetAllowedConnectionTypesForUpdate(&allowed_types)) {
    // The update setting is enforced by the device policy.

    // TODO(crbug.com/1054279): Use base::Contains after uprev to r680000.
    if (allowed_types.find(shill::kTypeCellular) == allowed_types.end()) {
      LOG(INFO) << "Disabling updates over metered network as it's not allowed "
                   "in the device policy.";
      return false;
    }

    LOG(INFO) << "Allowing updates over metered network per device policy.";
    return true;
  }

  // If there's no update setting in the device policy, we do not check
  // the local user setting here, which should be checked by
  // |OmahaRequestAction| during checking for update.
  LOG(INFO) << "Allowing updates over metered network as device policy does "
               "not include update setting.";
  return true;
}

bool ConnectionManager::IsAllowedConnectionTypesForUpdateSet() const {
  const policy::DevicePolicy* device_policy =
      SystemState::Get()->device_policy();
  if (!device_policy) {
    LOG(INFO) << "There's no device policy loaded yet.";
    return false;
  }

  set<string> allowed_types;
  if (!device_policy->GetAllowedConnectionTypesForUpdate(&allowed_types)) {
    return false;
  }

  return true;
}

bool ConnectionManager::GetConnectionProperties(ConnectionType* out_type,
                                                bool* out_metered) {
  dbus::ObjectPath default_service_path;
  TEST_AND_RETURN_FALSE(GetDefaultServicePath(&default_service_path));
  if (!default_service_path.IsValid()) {
    return false;
  }
  // Shill uses the "/" service path to indicate that it is not connected.
  if (default_service_path.value() == "/") {
    *out_type = ConnectionType::kDisconnected;
    *out_metered = false;
    return true;
  }
  TEST_AND_RETURN_FALSE(
      GetServicePathProperties(default_service_path, out_type, out_metered));
  return true;
}

bool ConnectionManager::GetDefaultServicePath(dbus::ObjectPath* out_path) {
  brillo::VariantDictionary properties;
  brillo::ErrorPtr error;
  ManagerProxyInterface* manager_proxy = shill_proxy_->GetManagerProxy();
  if (!manager_proxy) {
    return false;
  }
  TEST_AND_RETURN_FALSE(manager_proxy->GetProperties(&properties, &error));

  const auto& prop_default_service =
      properties.find(shill::kDefaultServiceProperty);
  if (prop_default_service == properties.end()) {
    return false;
  }

  *out_path = prop_default_service->second.TryGet<dbus::ObjectPath>();
  return out_path->IsValid();
}

bool ConnectionManager::GetServicePathProperties(const dbus::ObjectPath& path,
                                                 ConnectionType* out_type,
                                                 bool* out_metered) {
  // We create and dispose the ServiceProxyInterface on every request.
  std::unique_ptr<ServiceProxyInterface> service =
      shill_proxy_->GetServiceForPath(path);

  brillo::VariantDictionary properties;
  brillo::ErrorPtr error;
  TEST_AND_RETURN_FALSE(service->GetProperties(&properties, &error));

  // Populate the out_metered property.
  const auto& prop_metered = properties.find(shill::kMeteredProperty);
  if (prop_metered == properties.end() ||
      !prop_metered->second.IsTypeCompatible<bool>()) {
    *out_metered = false;
  } else {
    *out_metered = prop_metered->second.Get<bool>();
  }

  // Populate the out_type property.
  const auto& prop_type = properties.find(shill::kTypeProperty);
  if (prop_type == properties.end()) {
    // Set to Unknown if not present.
    *out_type = ConnectionType::kUnknown;
    return false;
  }

  string type_str = prop_type->second.TryGet<string>();
  if (type_str == shill::kTypeVPN) {
    const auto& prop_physical =
        properties.find(shill::kPhysicalTechnologyProperty);
    if (prop_physical == properties.end()) {
      LOG(ERROR) << "No PhysicalTechnology property found for a VPN"
                    " connection (service: "
                 << path.value() << "). Returning default kUnknown value.";
      *out_type = ConnectionType::kUnknown;
    } else {
      *out_type = connection_utils::ParseConnectionType(
          prop_physical->second.TryGet<string>());
    }
  } else {
    *out_type = connection_utils::ParseConnectionType(type_str);
  }
  return true;
}

}  // namespace chromeos_update_engine
