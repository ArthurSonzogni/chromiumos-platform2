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
#include "login_manager/dbus_proxies/org.chromium.ArcManager.h"

namespace login_manager {

// Helps splitting ArcManager D-Bus daemon from session_manager process.
class ArcManagerProxy {
 public:
  class Observer : public base::CheckedObserver {
   public:
    ~Observer() override = default;
    virtual void OnArcInstanceStopped(uint32_t value) {}
  };

  virtual ~ArcManagerProxy() = default;

  virtual void AddObserver(Observer* observer) = 0;
  virtual void RemoveObserver(Observer* observer) = 0;

  virtual bool OnUserSessionStarted(const std::string& in_account_id) = 0;
  virtual bool EmitStopArcVmInstanceImpulse() = 0;
  virtual bool RequestJobExit(uint32_t reason) = 0;
  virtual bool EnsureJobExit(int64_t timeout_ms) = 0;
  virtual bool StartArcMiniContainer(
      brillo::ErrorPtr* error, const std::vector<uint8_t>& in_request) = 0;
  virtual bool UpgradeArcContainer(brillo::ErrorPtr* error,
                                   const std::vector<uint8_t>& in_request) = 0;
  virtual bool StopArcInstance(brillo::ErrorPtr* error,
                               const std::string& in_account_id,
                               bool in_should_backup_log) = 0;
  virtual bool SetArcCpuRestriction(brillo::ErrorPtr* error,
                                    uint32_t in_restriction_state) = 0;
  virtual bool EmitArcBooted(brillo::ErrorPtr* error,
                             const std::string& in_account_id) = 0;
  virtual bool GetArcStartTimeTicks(brillo::ErrorPtr* error,
                                    int64_t* out_start_time) = 0;
  virtual void EnableAdbSideload(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>>
          response) = 0;
  virtual void QueryAdbSideload(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>>
          response) = 0;
};

// ArcManagerProxy implementation for in-process ArcManager calls.
class ArcManagerProxyInProcess : public ArcManagerProxy,
                                 public ArcManager::Observer {
 public:
  explicit ArcManagerProxyInProcess(ArcManager& arc_manager);
  ~ArcManagerProxyInProcess() override;

  // ArcManagerProxy:
  void AddObserver(ArcManagerProxy::Observer* observer) override;
  void RemoveObserver(ArcManagerProxy::Observer* observer) override;

  bool OnUserSessionStarted(const std::string& in_account_id) override;
  bool EmitStopArcVmInstanceImpulse() override;
  bool RequestJobExit(uint32_t reason) override;
  bool EnsureJobExit(int64_t timeout_ms) override;
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

  // ArcManager::Observer:
  void OnArcInstanceStopped(uint32_t value) override;

 private:
  const raw_ref<ArcManager> arc_manager_;
  base::ScopedObservation<ArcManager, ArcManager::Observer> observation_{this};

  // Invocation of `observers_` is the responsibility of subclasses.
  base::ObserverList<ArcManagerProxy::Observer> observers_;
};

// ArcManagerProxy implementation for D-Bus ArcManager calls.
class ArcManagerProxyDBus : public ArcManagerProxy {
 public:
  explicit ArcManagerProxyDBus(scoped_refptr<dbus::Bus> bus);
  ~ArcManagerProxyDBus() override;

  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;

  bool OnUserSessionStarted(const std::string& in_account_id) override;
  bool EmitStopArcVmInstanceImpulse() override;
  bool RequestJobExit(uint32_t reason) override;
  bool EnsureJobExit(int64_t timeout_ms) override;
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

 private:
  void OnArcInstanceStopped(uint32_t value);

  org::chromium::ArcManagerProxy arc_manager_;

  // Invocation of `observers_` is the responsibility of subclasses.
  base::ObserverList<Observer> observers_;
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_ARC_MANAGER_PROXY_H_
