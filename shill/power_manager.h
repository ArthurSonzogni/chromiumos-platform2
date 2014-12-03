// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_POWER_MANAGER_H_
#define SHILL_POWER_MANAGER_H_

// This class instantiates a PowerManagerProxy and distributes power events to
// registered users.  It also provides a means for calling methods on the
// PowerManagerProxy.

#include <map>
#include <memory>
#include <string>

#include <base/callback.h>
#include <base/cancelable_callback.h>

#include "shill/power_manager_proxy_interface.h"

namespace shill {

class DBusManager;
class DBusNameWatcher;
class EventDispatcher;
class ProxyFactory;

class PowerManager : public PowerManagerProxyDelegate {
 public:
  // This callback is called prior to a suspend attempt.  When it is OK for the
  // system to suspend, this callback should call ReportSuspendReadiness().
  typedef base::Closure SuspendImminentCallback;

  // This callback is called after the completion of a suspend attempt.  The
  // receiver should undo any pre-suspend work that was done by the
  // SuspendImminentCallback.
  // The receiver should be aware that it is possible to get a
  // SuspendDoneCallback while processing a DarkSuspendImminentCallback. So,
  // SuspendDoneCallback should be ready to run concurrently with (and in a
  // sense override) the actions taken by DarkSuspendImminentCallback.
  typedef base::Closure SuspendDoneCallback;

  // This callback is called at the beginning of a dark resume.
  // The receiver should arrange for ReportDarkSuspendImminentReadiness() to be
  // called when shill is ready to resuspend. In most cases,
  // ReportDarkSuspendImminentReadiness will be called asynchronously.
  typedef base::Closure DarkSuspendImminentCallback;

  // |proxy_factory| creates the PowerManagerProxy.  Usually this is
  // ProxyFactory::GetInstance().  Use a fake for testing.
  // Note: |Start| should be called to initialize this object before using it.
  PowerManager(EventDispatcher *dispatcher, ProxyFactory *proxy_factory);
  ~PowerManager() override;

  bool suspending() const { return suspending_; }
  bool in_dark_resume() const { return in_dark_resume_; }

  // Starts the PowerManager: Registers a suspend delay with the power manager
  // for |suspend_delay|. See PowerManagerProxyInterface::RegisterSuspendDelay()
  // for information about |suspend_delay|.
  // - |imminent_callback| will be invoked when a suspend attempt is commenced
  // - |done_callback| will be invoked when the attempt is completed. Returns
  //   false on failure.
  // - This object guarantees that a call to |imminent_callback| is followed by
  //   a call to |done_callback| (before any more calls to |imminent_callback|).
  // Requires a |DBusManager| that has been |Start|'ed.
  virtual void Start(
      DBusManager *dbus_manager,
      base::TimeDelta suspend_delay,
      const SuspendImminentCallback &suspend_imminent_callback,
      const SuspendDoneCallback &suspend_done_callback,
      const DarkSuspendImminentCallback &dark_suspend_imminent_callback);
  virtual void Stop();

  // Report suspend readiness. If called when there is no suspend attempt
  // active, this function will fail. Returns true if sucessfully reported to
  // powerd.
  virtual bool ReportSuspendReadiness();

  // Report dark suspend readiness. See ReportSuspendReadiness for more details.
  virtual bool ReportDarkSuspendReadiness();

  // Methods inherited from PowerManagerProxyDelegate.
  virtual void OnSuspendImminent(int suspend_id);
  virtual void OnSuspendDone(int suspend_id);
  virtual void OnDarkSuspendImminent(int suspend_id);

 private:
  friend class ManagerTest;
  friend class PowerManagerTest;
  friend class ServiceTest;

  // Human-readable string describing the suspend delay that is registered
  // with the power manager.
  static const int kInvalidSuspendId;
  static const char kSuspendDelayDescription[];
  static const char kDarkSuspendDelayDescription[];
  static const int kSuspendTimeoutMilliseconds;

  // These functions track the power_manager daemon appearing/vanishing from the
  // DBus connection.
  void OnPowerManagerAppeared(const std::string &name,
                              const std::string &owner);
  void OnPowerManagerVanished(const std::string &name);

  EventDispatcher *dispatcher_;

  // The power manager proxy created by this class.  It dispatches the inherited
  // delegate methods of this object when changes in the power state occur.
  const std::unique_ptr<PowerManagerProxyInterface> power_manager_proxy_;
  std::unique_ptr<DBusNameWatcher> power_manager_name_watcher_;
  // The delay (in milliseconds) to request powerd to wait after a suspend
  // notification is received. powerd will actually suspend the system at least
  // |suspend_delay_| after the notification, if we do not
  // |ReportSuspendReadiness| earlier.
  base::TimeDelta suspend_delay_;
  // powerd tracks each (dark) suspend delay requested (by different clients)
  // using randomly generated unique |(dark)suspend_delay_id_|s.
  bool suspend_delay_registered_;
  int suspend_delay_id_;
  bool dark_suspend_delay_registered_;
  int dark_suspend_delay_id_;
  // Callbacks from shill called by this object when:
  // ... powerd notified us that a suspend is imminent.
  SuspendImminentCallback suspend_imminent_callback_;
  // ... powerd notified us that the suspend attempt has finished.
  SuspendDoneCallback suspend_done_callback_;
  // ... powerd notified us that a dark suspend is imminent. This means that we
  // just entered dark resume.
  DarkSuspendImminentCallback dark_suspend_imminent_callback_;

  // Set to true by OnSuspendImminent() and to false by OnSuspendDone().
  bool suspending_;
  // Set to true by OnDarkSuspendImminent() and to false by OnSuspendDone().
  bool in_dark_resume_;
  int current_suspend_id_;
  int current_dark_suspend_id_;

  DISALLOW_COPY_AND_ASSIGN(PowerManager);
};

}  // namespace shill

#endif  // SHILL_POWER_MANAGER_H_
