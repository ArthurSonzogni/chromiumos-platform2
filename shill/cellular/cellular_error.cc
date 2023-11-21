// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/cellular_error.h"

#include <string>

#include <base/containers/fixed_flat_map.h>
#include <ModemManager/ModemManager.h>
#include <libmbim-glib/libmbim-glib.h>

// TODO(armansito): Once we refactor the code to handle the ModemManager D-Bus
// bindings in a dedicated class, this code should move there.
// (See crbug.com/246425)

namespace shill {

// static
void CellularError::FromMM1ChromeosDBusError(brillo::Error* dbus_error,
                                             Error* error) {
  if (!error)
    return;

  if (!dbus_error) {
    error->Reset();
    return;
  }

  const std::string name = dbus_error->GetCode();
  const std::string msg = dbus_error->GetMessage();
  Error::Type type;

  // TODO(b/217612447): How can we prevent a change in MM from messing up
  // the hardcoded strings?
  static constexpr auto errorMapping =
      base::MakeFixedFlatMap<std::string_view, Error::Type>({
          {MM_MOBILE_EQUIPMENT_ERROR_DBUS_PREFIX ".IncorrectPassword",
           Error::kIncorrectPin},
          {MM_MOBILE_EQUIPMENT_ERROR_DBUS_PREFIX ".Unknown",
           Error::kInternalError},
          {MM_CORE_ERROR_DBUS_PREFIX ".Throttled", Error::kInvalidApn},
          {MM_MOBILE_EQUIPMENT_ERROR_DBUS_PREFIX ".Ipv4OnlyAllowed",
           Error::kInvalidApn},
          {MM_MOBILE_EQUIPMENT_ERROR_DBUS_PREFIX ".Ipv6OnlyAllowed",
           Error::kInvalidApn},
          {MM_MOBILE_EQUIPMENT_ERROR_DBUS_PREFIX ".Ipv4v6OnlyAllowed",
           Error::kInvalidApn},
          {MM_MOBILE_EQUIPMENT_ERROR_DBUS_PREFIX ".MissingOrUnknownApn",
           Error::kInvalidApn},
          {MM_MOBILE_EQUIPMENT_ERROR_DBUS_PREFIX ".ServiceOptionNotSubscribed",
           Error::kInvalidApn},
          {MM_MOBILE_EQUIPMENT_ERROR_DBUS_PREFIX ".UserAuthenticationFailed",
           Error::kInvalidApn},
          {MM_MOBILE_EQUIPMENT_ERROR_DBUS_PREFIX ".NoCellsInArea",
           Error::kNoCarrier},
          {MM_MOBILE_EQUIPMENT_ERROR_DBUS_PREFIX ".PlmnNotAllowed",
           Error::kNoCarrier},
          {MM_MOBILE_EQUIPMENT_ERROR_DBUS_PREFIX
           ".ServiceOptionNotAuthorizedInPlmn",
           Error::kNoCarrier},
          {MM_MOBILE_EQUIPMENT_ERROR_DBUS_PREFIX ".ServingNetworkNotAuthorized",
           Error::kNoCarrier},
          {MBIM_STATUS_ERROR_DBUS_PREFIX ".OperationNotAllowed",
           Error::kOperationNotAllowed},
          {MM_MOBILE_EQUIPMENT_ERROR_DBUS_PREFIX ".SimPuk", Error::kPinBlocked},
          {MM_MOBILE_EQUIPMENT_ERROR_DBUS_PREFIX ".SimPin",
           Error::kPinRequired},
          {MM_CORE_ERROR_DBUS_PREFIX ".WrongState", Error::kWrongState},
      });
  const auto it = errorMapping.find(name);
  if (it != errorMapping.end())
    type = it->second;
  else
    type = Error::kOperationFailed;

  if (!msg.empty())
    return error->Populate(type, msg, name);
  else
    return error->Populate(type, "", name);
}
}  // namespace shill
