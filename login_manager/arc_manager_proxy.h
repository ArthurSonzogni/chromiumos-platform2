// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_ARC_MANAGER_PROXY_H_
#define LOGIN_MANAGER_ARC_MANAGER_PROXY_H_

#include <memory>
#include <string>
#include <vector>

#include <base/memory/raw_ref.h>
#include <base/observer_list.h>
#include <base/scoped_observation.h>

#include "login_manager/arc_manager.h"
#include "login_manager/dbus_adaptors/org.chromium.ArcManager.h"

namespace login_manager {

// Helps splitting ArcManager D-Bus daemon from session_manager process.
class ArcManagerProxy : public org::chromium::ArcManagerInterface {
 public:
  class Observer : public base::CheckedObserver {
   public:
    ~Observer() override = default;
    virtual void OnArcInstanceStopped(uint32_t value) {}
  };

  virtual void AddObserver(Observer* observer) = 0;
  virtual void RemoveObserver(Observer* observer) = 0;
};

// Skeleton implementation of ArcManagerProxy using
// org::chromium::ArcManagerInterface.
class ArcManagerProxyBase : public ArcManagerProxy {
 public:
  // org::chromium::ArcManagerInterface:
  void OnUserSessionStarted(const std::string& in_account_id) override;
  bool StartArcMiniContainer(brillo::ErrorPtr* error,
                             const std::vector<uint8_t>& in_request) override;
  bool UpgradeArcContainer(brillo::ErrorPtr* error,
                           const std::vector<uint8_t>& in_request) override;
  bool StopArcInstance(brillo::ErrorPtr* error,
                       const std::string& in_account_id,
                       bool in_should_backup_log) override;
  bool SetArcCpuRestriction(brillo::ErrorPtr* error,
                            uint32_t in_restriction_state) override;
  bool EmitArcBooted(brillo::ErrorPtr* error,
                     const std::string& in_account_id) override;
  bool GetArcStartTimeTicks(brillo::ErrorPtr* error,
                            int64_t* out_start_time) override;
  void EnableAdbSideload(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response)
      override;
  void QueryAdbSideload(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response)
      override;

  // ArcManagerProxy:
  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;

 protected:
  explicit ArcManagerProxyBase(org::chromium::ArcManagerInterface& arc_manager);
  ~ArcManagerProxyBase() override;

  const raw_ref<org::chromium::ArcManagerInterface> arc_manager_;

  // Invocation of `observers_` is the responsibility of subclasses.
  base::ObserverList<Observer> observers_;
};

// Uses in-process ArcManager object to support ARC operations.
class ArcManagerProxyInProcess : public ArcManagerProxyBase,
                                 public ArcManager::Observer {
 public:
  explicit ArcManagerProxyInProcess(ArcManager& arc_manager);
  ~ArcManagerProxyInProcess() override;

  // ArcManager::Observer:
  void OnArcInstanceStopped(uint32_t value) override;

 private:
  base::ScopedObservation<ArcManager, ArcManager::Observer> observation_{this};
};

// TODO(crbug.com/390297821): Implement D-Bus based ArcManagerProxy.

}  // namespace login_manager

#endif  // LOGIN_MANAGER_ARC_MANAGER_PROXY_H_
