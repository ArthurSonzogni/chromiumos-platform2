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
#include "base/file_path.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/string_util.h"
#include "base/stringprintf.h"
#include "chromeos/dbus/dbus.h"
#include "chromeos/dbus/service_constants.h"
#include "power_manager/powerd/ambient_light_sensor.h"
#include "power_manager/powerd/external_backlight_controller.h"
#include "power_manager/powerd/idle_detector.h"
#include "power_manager/powerd/internal_backlight_controller.h"
#include "power_manager/powerd/monitor_reconfigure.h"
#include "power_manager/powerd/powerd.h"
#include "power_manager/powerd/system/external_backlight.h"
#include "power_manager/powerd/system/internal_backlight.h"
#include "power_manager/powerd/video_detector.h"

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

  scoped_ptr<power_manager::AmbientLightSensor> light_sensor;
#ifndef IS_DESKTOP
  light_sensor.reset(new power_manager::AmbientLightSensor());
  light_sensor->Init();
#endif

  power_manager::MonitorReconfigure monitor_reconfigure;

#ifdef IS_DESKTOP
  power_manager::system::ExternalBacklight display_backlight;
  if (!display_backlight.Init())
    LOG(WARNING) << "Cannot initialize display backlight";
  power_manager::ExternalBacklightController display_backlight_controller(
      &display_backlight);
#else
  power_manager::system::InternalBacklight display_backlight;
  if (!display_backlight.Init(FilePath(power_manager::kInternalBacklightPath),
                              power_manager::kInternalBacklightPattern))
    LOG(WARNING) << "Cannot initialize display backlight";
  power_manager::InternalBacklightController display_backlight_controller(
      &display_backlight, &prefs, light_sensor.get());
#endif

  display_backlight_controller.SetMonitorReconfigure(&monitor_reconfigure);
  if (!display_backlight_controller.Init())
    LOG(WARNING) << "Cannot initialize display backlight controller";

  scoped_ptr<power_manager::KeyboardBacklightController>
      keyboard_backlight_controller;
#ifdef HAS_KEYBOARD_BACKLIGHT
  scoped_ptr<power_manager::system::InternalBacklight> keyboard_backlight(
      new power_manager::system::InternalBacklight);
  if (keyboard_backlight->Init(FilePath(power_manager::kKeyboardBacklightPath),
                               power_manager::kKeyboardBacklightPattern)) {
    keyboard_backlight_controller.reset(
        new power_manager::KeyboardBacklightController(
            keyboard_backlight.get(), &prefs, light_sensor.get()));
    if (!keyboard_backlight_controller->Init()) {
      LOG(WARNING) << "Cannot initialize keyboard backlight controller!";
      keyboard_backlight_controller.reset();
    }
  } else {
    LOG(WARNING) << "Cannot initialize keyboard backlight!";
    keyboard_backlight.reset();
  }
#endif

  MetricsLibrary metrics_lib;
  power_manager::VideoDetector video_detector;
  video_detector.Init();
  if (keyboard_backlight_controller.get())
    video_detector.AddObserver(keyboard_backlight_controller.get());
  power_manager::IdleDetector idle;
  metrics_lib.Init();
  FilePath run_dir(FLAGS_run_dir);
  power_manager::Daemon daemon(&display_backlight_controller,
                               &prefs,
                               &metrics_lib,
                               &video_detector,
                               &idle,
                               keyboard_backlight_controller.get(),
                               run_dir);

  daemon.Init();
  daemon.Run();
  return 0;
}
