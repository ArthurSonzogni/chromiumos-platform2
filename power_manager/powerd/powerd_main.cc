// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gflags/gflags.h>
#include <glib.h>
#include <syslog.h>

#include <string>

// Defines from syslog.h that conflict with base/logging.h. Ugh.
#undef LOG_INFO
#undef LOG_WARNING

#include "base/command_line.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/string_util.h"
#include "base/stringprintf.h"
#include "chromeos/dbus/dbus.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/ambient_light_sensor.h"
#include "power_manager/powerd/audio_detector.h"
#include "power_manager/powerd/backlight.h"
#include "power_manager/powerd/idle_detector.h"
#include "power_manager/powerd/monitor_reconfigure.h"
#include "power_manager/powerd/powerd.h"
#include "power_manager/powerd/video_detector.h"
#include "system_api/dbus/service_constants.h"

#ifdef IS_DESKTOP
#include "power_manager/powerd/external_backlight_client.h"
#include "power_manager/powerd/external_backlight_controller.h"
#else
#include "power_manager/powerd/internal_backlight_controller.h"
#endif

#ifndef VCSID
#define VCSID "<not set>"
#endif

using std::string;

DEFINE_string(prefs_dir, "",
              "Directory to store settings.");
DEFINE_string(default_prefs_dir, "",
              "Directory to read default settings (Read Only).");
DEFINE_string(log_dir, "",
              "Directory to store logs.");
DEFINE_string(run_dir, "",
              "Directory to store stateful data for daemon.");

namespace {

// Returns true on success.
bool SetUpLogSymlink(const string& symlink_path, const string& log_basename) {
  unlink(symlink_path.c_str());
  if (symlink(log_basename.c_str(), symlink_path.c_str()) == -1) {
    PLOG(ERROR) << "Unable to create symlink " << symlink_path
                << " pointing at " << log_basename;
    return false;
  }
  return true;
}

string GetTimeAsString(time_t utime) {
  struct tm tm;
  CHECK_EQ(localtime_r(&utime, &tm), &tm);
  char str[16];
  CHECK_EQ(strftime(str, sizeof(str), "%Y%m%d-%H%M%S", &tm), 15UL);
  return string(str);
}

// Helper function for WaitForPowerM().  Inspects D-Bus NameOwnerChanged signals
// and quits |data|, a GMainLoop*, when it sees one about powerm.
void HandleDBusNameOwnerChanged(DBusGProxy* proxy,
                                const gchar* name,
                                const gchar* old_owner,
                                const gchar* new_owner,
                                void* data) {
  LOG(INFO) << "Got signal about " << name << " ownership change "
            << "(\"" << old_owner << "\" -> \"" << new_owner << "\")";
  if (strcmp(name, power_manager::kRootPowerManagerServiceName) == 0)
    g_main_loop_quit(static_cast<GMainLoop*>(data));
}

// Blocks until powerm has registered with D-Bus.
void WaitForPowerM() {
  // Listen for a D-Bus ownership change on powerm's name.
  const char kNameOwnerChangedSignal[] = "NameOwnerChanged";
  GMainLoop* loop = g_main_loop_new(NULL, false);
  chromeos::dbus::Proxy proxy(chromeos::dbus::GetSystemBusConnection(),
                              DBUS_SERVICE_DBUS,
                              DBUS_PATH_DBUS,
                              DBUS_INTERFACE_DBUS);
  dbus_g_proxy_add_signal(proxy.gproxy(), kNameOwnerChangedSignal,
                          G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                          G_TYPE_INVALID);
  dbus_g_proxy_connect_signal(proxy.gproxy(), kNameOwnerChangedSignal,
                              G_CALLBACK(HandleDBusNameOwnerChanged),
                              loop, NULL);

  // Create a proxy to test if powerm is running.
  // dbus_g_proxy_new_for_name_owner() is documented as returning NULL if the
  // passed-in name has no owner.
  GError* error = NULL;
  DBusGProxy* powerm_proxy = dbus_g_proxy_new_for_name_owner(
      chromeos::dbus::GetSystemBusConnection().g_connection(),
      power_manager::kRootPowerManagerServiceName,
      power_manager::kPowerManagerServicePath,
      power_manager::kRootPowerManagerInterface,
      &error);

  if (powerm_proxy) {
    LOG(ERROR) << "powerm is already running at \""
               << dbus_g_proxy_get_bus_name(powerm_proxy) << "\"";
    g_object_unref(powerm_proxy);
  } else {
    // If powerm isn't running yet, spin up an event loop that will run until we
    // get notification that its name has been claimed.
    g_error_free(error);
    LOG(INFO) << "Waiting for " << kNameOwnerChangedSignal
              << " signal indicating that powerm has started";
    g_main_loop_run(loop);
  }
  g_main_loop_unref(loop);
}

}  // namespace

int main(int argc, char* argv[]) {
  // Sadly we can't use LOG() here - we always want this message logged, even
  // when other logging is turned off.
  openlog("powerd", LOG_PID, LOG_DAEMON);
  syslog(LOG_NOTICE, "vcsid %s", VCSID);
  closelog();
  google::ParseCommandLineFlags(&argc, &argv, true);
  CHECK(!FLAGS_prefs_dir.empty()) << "--prefs_dir is required";
  CHECK(!FLAGS_log_dir.empty()) << "--log_dir is required";
  CHECK(!FLAGS_run_dir.empty()) << "--run_dir is required";
  CHECK_EQ(argc, 1) << "Unexpected arguments. Try --help";
  CommandLine::Init(argc, argv);

  const string log_latest =
      StringPrintf("%s/powerd.LATEST", FLAGS_log_dir.c_str());
  const string log_basename =
      StringPrintf("powerd.%s", GetTimeAsString(::time(NULL)).c_str());
  const string log_path = FLAGS_log_dir + "/" + log_basename;
  CHECK(SetUpLogSymlink(log_latest, log_basename));
  logging::InitLogging(log_path.c_str(),
                       logging::LOG_ONLY_TO_FILE,
                       logging::DONT_LOCK_LOG_FILE,
                       logging::APPEND_TO_OLD_LOG_FILE,
                       logging::DISABLE_DCHECK_FOR_NON_OFFICIAL_RELEASE_BUILDS);

  FilePath prefs_dir(FLAGS_prefs_dir);
  FilePath default_prefs_dir(FLAGS_default_prefs_dir.empty() ?
                             "/usr/share/power_manager" :
                             FLAGS_default_prefs_dir);
  std::vector<FilePath> pref_paths;
  pref_paths.push_back(prefs_dir);
  pref_paths.push_back(default_prefs_dir.Append("board_specific"));
  pref_paths.push_back(default_prefs_dir);
  power_manager::PowerPrefs prefs(pref_paths);
  g_type_init();

  WaitForPowerM();

  scoped_ptr<power_manager::AmbientLightSensor> als;
#ifndef IS_DESKTOP
  als.reset(new power_manager::AmbientLightSensor());
  if (!als->Init(&prefs))
    LOG(WARNING) << "Cannot initialize light sensor";
#endif

  power_manager::MonitorReconfigure monitor_reconfigure;
#ifdef IS_DESKTOP
  power_manager::ExternalBacklightClient backlight;
  if (!backlight.Init())
    LOG(WARNING) << "Cannot initialize backlight";
#else
  power_manager::Backlight backlight;
  if (!backlight.Init(FilePath(power_manager::kBacklightPath),
                      power_manager::kBacklightPattern))
    LOG(WARNING) << "Cannot initialize backlight";
#endif

#ifdef IS_DESKTOP
  power_manager::ExternalBacklightController backlight_ctl(&backlight);
#else
  power_manager::InternalBacklightController backlight_ctl(&backlight,
                                                           &prefs,
                                                           als.get());
#endif
  backlight_ctl.SetMonitorReconfigure(&monitor_reconfigure);
  if (!backlight_ctl.Init())
    LOG(WARNING) << "Cannot initialize backlight controller";

  scoped_ptr<power_manager::KeyboardBacklightController> keyboard_controller;
#ifdef HAS_KEYBOARD_BACKLIGHT
  scoped_ptr<power_manager::Backlight> keyboard_light(
      new power_manager::Backlight());
  if (!keyboard_light->Init(FilePath(power_manager::kKeyboardBacklightPath),
                            power_manager::kKeyboardBacklightPattern)) {
    LOG(WARNING) << "Cannot initialize keyboard backlight!";
    keyboard_light.reset(NULL);
  }
  if (keyboard_light.get()) {
    keyboard_controller.reset(
        new power_manager::KeyboardBacklightController(keyboard_light.get(),
                                                       &prefs,
                                                       als.get()));
    if (!keyboard_controller->Init()) {
      LOG(WARNING) << "Cannot initialize keyboard controller!";
      keyboard_controller.reset(NULL);
    }
  }
#endif

  MetricsLibrary metrics_lib;
  power_manager::VideoDetector video_detector;
  video_detector.Init();
  if (keyboard_controller.get())
    video_detector.AddObserver(keyboard_controller.get());
  power_manager::AudioDetector audio_detector;
  audio_detector.Init();
  power_manager::IdleDetector idle;
  metrics_lib.Init();
  FilePath run_dir(FLAGS_run_dir);
  power_manager::Daemon daemon(&backlight_ctl,
                               &prefs,
                               &metrics_lib,
                               &video_detector,
                               &audio_detector,
                               &idle,
                               keyboard_controller.get(),
                               als.get(),
                               run_dir);

  daemon.Init();
  daemon.Run();
  return 0;
}
