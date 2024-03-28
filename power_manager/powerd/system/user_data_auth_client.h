// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_USER_DATA_AUTH_CLIENT_H_
#define POWER_MANAGER_POWERD_SYSTEM_USER_DATA_AUTH_CLIENT_H_

#include <string>

#include <base/files/file_path.h>
#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>
#include <base/observer_list.h>

#include "power_manager/powerd/system/dbus_wrapper.h"
#include "power_manager/powerd/system/suspend_freezer.h"

namespace dbus {
class ObjectProxy;
class Response;
class Signal;
}  // namespace dbus

namespace power_manager::system {

// Implementation that allows power_manager to communicated with cryptohomed.
class UserDataAuthClient {
 public:
  // This callback is called after the device key has been restored.
  using DeviceKeyRestoredCallback = base::RepeatingClosure;

  UserDataAuthClient();
  UserDataAuthClient(const UserDataAuthClient&) = delete;
  UserDataAuthClient& operator=(const UserDataAuthClient&) = delete;

  virtual ~UserDataAuthClient() = default;

  // Initializes the object. Ownership of |dbus_wrapper| remains with the
  // caller.
  void Init(DBusWrapperInterface* dbus_wrapper,
            const DeviceKeyRestoredCallback& device_key_restored_callback);

  // Evicts the device key from the logged in user's cryptphome. All the user's
  // encrypted home directory will not be accessible after this action.
  virtual void EvictDeviceKey(int suspend_request_id);

 private:
  void HandleKeyRestoredSignal(dbus::Signal* signal);

  DBusWrapperInterface* dbus_wrapper_ = nullptr;  // weak
  // The |user_data_auth_dbus_proxy_| is owned by |dbus_wrapper_|
  dbus::ObjectProxy* user_data_auth_dbus_proxy_ = nullptr;  // weak

  // The callback that is called after the device key has been restored.
  DeviceKeyRestoredCallback device_key_restored_callback_;

  // This member should be last.
  base::WeakPtrFactory<UserDataAuthClient> weak_ptr_factory_;
};

}  // namespace power_manager::system

#endif  // POWER_MANAGER_POWERD_SYSTEM_USER_DATA_AUTH_CLIENT_H_
