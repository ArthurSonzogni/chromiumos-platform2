// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_ARC_MANAGER_H_
#define LOGIN_MANAGER_ARC_MANAGER_H_

#include <memory>

#include <base/memory/raw_ref.h>
#include <base/memory/weak_ptr.h>

#include "login_manager/arc_sideload_status_interface.h"

namespace brillo::dbus_utils {
template <typename... Ts>
class DBusMethodResponse;
}  // namespace brillo::dbus_utils

namespace login_manager {

class ArcSideloadStatusInterface;
class SystemUtils;

// Manages ARC operations.
class ArcManager {
 public:
  ArcManager(SystemUtils& system_utils,
             std::unique_ptr<ArcSideloadStatusInterface> arc_sideload_status);
  ArcManager(const ArcManager&) = delete;
  ArcManager& operator=(const ArcManager&) = delete;
  ~ArcManager();

  void Initialize();
  void Finalize();

  // Returns whether ADB-sideloading is allowed.
  // TODO(crbug.com/390297821): encapsulate the use in this class.
  bool IsAdbSideloadAllowed() const;

  // D-Bus method implementation.
  void EnableAdbSideload(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response);
  void QueryAdbSideload(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response);

 private:
  void EnableAdbSideloadCallbackAdaptor(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
      ArcSideloadStatusInterface::Status status,
      const char* error);
  void QueryAdbSideloadCallbackAdaptor(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
      ArcSideloadStatusInterface::Status status);

  const raw_ref<SystemUtils> system_utils_;
  std::unique_ptr<ArcSideloadStatusInterface> arc_sideload_status_;

  base::WeakPtrFactory<ArcManager> weak_factory_{this};
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_ARC_MANAGER_H_
