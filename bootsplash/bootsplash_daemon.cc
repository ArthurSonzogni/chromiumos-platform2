// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootsplash/bootsplash_daemon.h"

#include <csignal>
#include <sysexits.h>

#include <base/logging.h>
#include <base/time/time.h>
#include <brillo/dbus/dbus_signal_handler.h>
#include <brillo/errors/error.h>
#include <metrics/bootstat.h>

#include "bootsplash/session_manager_client.h"
#include "bootsplash/utils.h"

namespace bootsplash {

static BootSplashDaemon* g_bootsplash_daemon = nullptr;

constexpr int kFramesPerSecond = 30;
constexpr int64_t kBootLogoAnimationIntervalMilliseconds =
    base::Time::kMillisecondsPerSecond / kFramesPerSecond;

namespace internal {
/* static */
BootSplashDaemon* Get() {
  CHECK(g_bootsplash_daemon) << "BootSplashDaemon::Get() called before ctor()";
  return g_bootsplash_daemon;
}

/* static */
void SignalHandler(int sig) {
  CHECK_EQ(sig, SIGUSR1);

  Get()->DBusDaemonInit();
}
}  // namespace internal

BootSplashDaemon::BootSplashDaemon(bool feature_simon_enabled)
    : feature_simon_enabled_(feature_simon_enabled) {
  g_bootsplash_daemon = this;

  num_frames_ = utils::GetMaxBootSplashFrameNumber(feature_simon_enabled_);

  signal(SIGUSR1, &internal::SignalHandler);
}

void BootSplashDaemon::DBusDaemonInit() {
  int return_code = brillo::DBusDaemon::OnInit();
  if (return_code != EX_OK) {
    LOG(ERROR) << "Failed to init brillo::DBusDaemon: " << return_code;
    return;
  }
  CHECK(bus_) << "Failed to connect to dBus";

  // Must be after DBusDaemon::OnInit(), so |bus_| is initialized.
  session_manager_client_ = SessionManagerClient::Create(bus_);
  CHECK(session_manager_client_) << "Failed to initialize SessionManagerClient";
  session_manager_client_->AddObserver(this);

  LOG(INFO) << "DBus initialized.";
}

int BootSplashDaemon::InitBootSplash() {
  frecon->UpdateBootLogoDisplay(0);
  StartBootLogoAnimationAlarm();

  // Drop DRM master so Chrome can show the login screen as soon as it's ready.
  frecon->DropDrmMaster();

  return EX_OK;
}

int BootSplashDaemon::OnInit() {
  frecon = Frecon::Create(feature_simon_enabled_);
  if (!frecon) {
    LOG(ERROR) << "Failed to create Frecon object.";
    return -EX_SOFTWARE;
  }

  int status;
  status = InitBootSplash();
  if (status) {
    return status;
  }

  bootstat::BootStat().LogEvent("splash-screen-visible");

  return EX_OK;
}

void BootSplashDaemon::ShutdownBootSplash() {
  boot_logo_animation_alarm_->Stop();
}

void BootSplashDaemon::OnShutdown(int* return_code) {
  LOG(INFO) << "Shutting down.";

  *return_code = EX_OK;
  brillo::DBusDaemon::OnShutdown(return_code);
  ShutdownBootSplash();
}

void BootSplashDaemon::SessionManagerLoginPromptVisibleEventReceived() {
  LOG(INFO) << "LoginPromptVisible dbus signal received";
  Quit();
}

void BootSplashDaemon::StartBootLogoAnimationAlarm() {
  boot_logo_animation_alarm_->Start(
      FROM_HERE, base::Milliseconds(kBootLogoAnimationIntervalMilliseconds),
      base::BindRepeating(&BootSplashDaemon::OnBootLogoAnimationAlarmFired,
                          base::Unretained(this)));
}

void BootSplashDaemon::OnBootLogoAnimationAlarmFired() {
  static bool ascending = true;
  static int currentFrame = 0;

  frecon->UpdateBootLogoDisplay(currentFrame);

  if (ascending) {
    if (currentFrame == num_frames_) {
      ascending = false;
      --currentFrame;
    } else {
      ++currentFrame;
    }
  } else {
    if (currentFrame == 0) {
      ascending = true;
      ++currentFrame;
    } else {
      --currentFrame;
    }
  }

  boot_logo_animation_alarm_->Reset();
}

}  // namespace bootsplash
