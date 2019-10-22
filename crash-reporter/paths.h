// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_PATHS_H_
#define CRASH_REPORTER_PATHS_H_

#include <base/files/file_path.h>
#include <base/strings/string_piece.h>

namespace paths {

// Directory where we keep various state flags.
constexpr char kSystemRunStateDirectory[] = "/run/crash_reporter";

// Subdirectory to store crashes that occur when persistent storage is not
// available.
constexpr char kSystemRunCrashDirectory[] = "/run/crash_reporter/crash";

// Directory where crash_reporter stores flag for metrics_daemon.
constexpr char kSystemRunMetricsFlagDirectory[] =
    "/run/metrics/external/crash-reporter";

// Directory where crash_reporter stores files (ex. saved version info).
constexpr char kCrashReporterStateDirectory[] = "/var/lib/crash_reporter";

// Directory where system crashes are saved.
constexpr char kSystemCrashDirectory[] = "/var/spool/crash";

// Directory to keep persist early crash failures.
constexpr char kStatefulClobberCrashDirectory[] =
    "/mnt/stateful_partition/unencrypted/preserve/crash";

// Directory where system configuration files are located.
constexpr char kEtcDirectory[] = "/etc";

// Directory where per-user crashes are saved before the user logs in.
//
// Normally this path is not used.  Unfortunately, there are a few edge cases
// where we need this.  Any process that runs as kDefaultUserName that crashes
// is consider a "user crash".  That includes the initial Chrome browser that
// runs the login screen.  If that blows up, there is no logged in user yet,
// so there is no per-user dir for us to stash things in.  Instead we fallback
// to this path as it is at least encrypted on a per-system basis.
//
// This also comes up when running autotests.  The GUI is sitting at the login
// screen while tests are sshing in, changing users, and triggering crashes as
// the user (purposefully).
constexpr char kFallbackUserCrashDirectory[] = "/home/chronos/crash";

// File whose existence indicates this is a developer image.
constexpr char kLeaveCoreFile[] = "/root/.leave_core";

// Base name of file whose existence indicates a crash test is currently
// running.
// This is used in integration tests, including tast.platform.KernelWarning and
// tast.platform.ServiceFailure. (see local/crash/crash.go in the tast-tests
// repo)
constexpr char kCrashTestInProgress[] = "crash-test-in-progress";

// Base name of file whose existence indicates that the anomaly detector is
// ready for anomalies.
constexpr char kAnomalyDetectorReady[] = "anomaly-detector-ready";

// Base name of file whose existence indicates uploading of device coredumps is
// allowed.
constexpr char kDeviceCoredumpUploadAllowed[] =
    "device_coredump_upload_allowed";

// Base name of file that contains Chrome OS version info.
constexpr char kLsbRelease[] = "lsb-release";

// Basename of file in the state directory that has the client ID.
constexpr char kClientId[] = "client_id";

// Gets a FilePath from the given path. A prefix will be added if the prefix is
// set with SetPrefixForTesting().
base::FilePath Get(base::StringPiece file_path);

// Gets a FilePath from the given directory and the base name. A prefix will be
// added if the prefix is set with SetPrefixForTesting().
base::FilePath GetAt(base::StringPiece directory, base::StringPiece base_name);

// Sets a prefix that'll be added when Get() is called, for unit testing.
// For example, if "/tmp" is set as the prefix, Get("/run/foo") will return
// "/tmp/run/foo". Passing "" will reset the prefix.
void SetPrefixForTesting(const base::FilePath& prefix);

}  // namespace paths

#endif  // CRASH_REPORTER_PATHS_H_
