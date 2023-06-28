// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BOOTSPLASH_BOOTSPLASH_DAEMON_H_
#define BOOTSPLASH_BOOTSPLASH_DAEMON_H_

#include <memory>
#include <string>

#include <brillo/daemons/dbus_daemon.h>
#include <brillo/timers/alarm_timer.h>
#include <gtest/gtest_prod.h>

#include "bootsplash/frecon.h"
#include "bootsplash/session_manager_client_interface.h"

namespace bootsplash {

class BootSplashDaemon : public brillo::DBusDaemon,
                         public SessionEventObserver {
 public:
  explicit BootSplashDaemon(bool feature_simon_enabled);
  BootSplashDaemon(const BootSplashDaemon&) = delete;
  BootSplashDaemon& operator=(const BootSplashDaemon&) = delete;
  ~BootSplashDaemon() override = default;

  // brillo::DBusDaemon overrides.
  int OnInit() override;
  void OnShutdown(int* return_code) override;

  // Implements SessionEventObserver.
  void SessionManagerLoginPromptVisibleEventReceived() override;

  virtual void DBusDaemonInit();

  void OnBootLogoAnimationAlarmFired();

 protected:
  // Unit Testing
  void OverrideBootLogoAnimationAlarmForTesting() {
    boot_logo_animation_alarm_ =
        brillo::timers::SimpleAlarmTimer::CreateForTesting();
  }

 private:
  int InitBootSplash();
  void ShutdownBootSplash();
  void StartBootLogoAnimationAlarm();

  std::unique_ptr<Frecon> frecon;
  int num_frames_;
  bool feature_simon_enabled_;

  std::unique_ptr<SessionManagerClientInterface> session_manager_client_;

  // Animate the boot logo.
  std::unique_ptr<brillo::timers::SimpleAlarmTimer> boot_logo_animation_alarm_ =
      brillo::timers::SimpleAlarmTimer::Create();

  // Unit Testing
  friend class BootSplashDaemonTest;
  FRIEND_TEST(BootSplashDaemonTest, InitBootSplash);
};

}  // namespace bootsplash

#endif  // BOOTSPLASH_BOOTSPLASH_DAEMON_H_
