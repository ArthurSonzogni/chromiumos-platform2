// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_CRYPTOHOME_CLIENT_H_
#define POWER_MANAGER_POWERD_SYSTEM_CRYPTOHOME_CLIENT_H_

#include <string>

#include <base/files/file_path.h>
#include <base/memory/weak_ptr.h>
#include <base/observer_list.h>

#include "power_manager/powerd/system/dbus_wrapper.h"

namespace dbus {
class ObjectProxy;
class Response;
class Signal;
}  // namespace dbus

namespace power_manager::system {

// Implementation that allows power_manager to communicated with cryptohomed.
class CryptohomeClient {
 public:
  // Used for testing only.
  CryptohomeClient();
  // Initializes the object. Ownership of |dbus_wrapper| remains with the
  // caller.
  explicit CryptohomeClient(DBusWrapperInterface* dbus_wrapper);
  CryptohomeClient(const CryptohomeClient&) = delete;
  CryptohomeClient& operator=(const CryptohomeClient&) = delete;

  virtual ~CryptohomeClient() = default;

  // Evicts the device key from the logged in user's cryptphome. All
  // the user's encrypted home directory will not be accsible after this action.
  virtual void EvictDeviceKey(int suspend_request_id);

 private:
  DBusWrapperInterface* dbus_wrapper_ = nullptr;   // weak
  dbus::ObjectProxy* cryptohome_proxy_ = nullptr;  // weak
};

}  // namespace power_manager::system

#endif  // POWER_MANAGER_POWERD_SYSTEM_CRYPTOHOME_CLIENT_H_
