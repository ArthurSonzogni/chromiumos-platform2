// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_ARC_MANAGER_PROXY_H_
#define LOGIN_MANAGER_ARC_MANAGER_PROXY_H_

#include <memory>
#include <string>

#include <base/memory/raw_ref.h>

#include "login_manager/arc_manager.h"
#include "login_manager/dbus_proxies/org.chromium.ArcManager.h"

namespace login_manager {

// Helps splitting ArcManager D-Bus daemon from session_manager process.
class ArcManagerProxy {
 public:
  virtual ~ArcManagerProxy() = default;

  virtual bool OnUserSessionStarted(const std::string& in_account_id) = 0;
  virtual bool EmitStopArcVmInstanceImpulse() = 0;
  virtual bool EmitArcBooted(brillo::ErrorPtr* error,
                             const std::string& in_account_id) = 0;
  virtual void EnableAdbSideload(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>>
          response) = 0;
  virtual void QueryAdbSideload(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>>
          response) = 0;
};

// ArcManagerProxy implementation for in-process ArcManager calls.
class ArcManagerProxyInProcess : public ArcManagerProxy {
 public:
  explicit ArcManagerProxyInProcess(ArcManager& arc_manager);
  ~ArcManagerProxyInProcess() override;

  // ArcManagerProxy:
  bool OnUserSessionStarted(const std::string& in_account_id) override;
  bool EmitStopArcVmInstanceImpulse() override;
  bool EmitArcBooted(brillo::ErrorPtr* error,
                     const std::string& in_account_id) override;
  void EnableAdbSideload(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response)
      override;
  void QueryAdbSideload(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response)
      override;

 private:
  const raw_ref<ArcManager> arc_manager_;
};

// ArcManagerProxy implementation for D-Bus ArcManager calls.
class ArcManagerProxyDBus : public ArcManagerProxy {
 public:
  explicit ArcManagerProxyDBus(scoped_refptr<dbus::Bus> bus);
  ~ArcManagerProxyDBus() override;

  bool OnUserSessionStarted(const std::string& in_account_id) override;
  bool EmitStopArcVmInstanceImpulse() override;
  bool EmitArcBooted(brillo::ErrorPtr* error,
                     const std::string& in_account_id) override;
  void EnableAdbSideload(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response)
      override;
  void QueryAdbSideload(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response)
      override;

 private:
  org::chromium::ArcManagerProxy arc_manager_;
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_ARC_MANAGER_PROXY_H_
