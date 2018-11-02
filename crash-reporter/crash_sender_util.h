// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_CRASH_SENDER_UTIL_H_
#define CRASH_REPORTER_CRASH_SENDER_UTIL_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/key_value_store.h>
#include <session_manager/dbus-proxies.h>

namespace util {

// Represents a name-value pair for an environment variable.
struct EnvPair {
  const char* name;
  const char* value;
};

// Predefined environment variables for controlling the behaviors of
// crash_sender.
//
// TODO(satorux): Remove the environment variables once the shell script is
// gone. The environment variables are handy in the shell script, but should not
// be needed in the C++ version.
constexpr EnvPair kEnvironmentVariables[] = {
    // Set this to 1 in the environment to allow uploading crash reports
    // for unofficial versions.
    {"FORCE_OFFICIAL", "0"},
    // Maximum crashes to send per day.
    {"MAX_CRASH_RATE", "32"},
    // Set this to 1 in the environment to pretend to have booted in developer
    // mode.  This is used by autotests.
    {"MOCK_DEVELOPER_MODE", "0"},
    // Ignore PAUSE_CRASH_SENDING file if set.
    {"OVERRIDE_PAUSE_SENDING", "0"},
    // Maximum time to sleep between sends.
    {"SECONDS_SEND_SPREAD", "600"},
};

// Parses the command line, and handles the command line flags.
//
// This function also sets the predefined environment valuables to the default
// values, or the values specified by -e options.
//
// On error, the process exits as a failure with an error message for the
// first-encountered error.
void ParseCommandLine(int argc, const char* const* argv);

// Returns true if mock is enabled.
bool IsMock();

// Returns true if the sending should be paused.
bool ShouldPauseSending();

// Checks if the dependencies used in the shell script exist. On error, returns
// false, and saves the first path that was missing in |missing_path|.
// TODO(satorux): Remove this once rewriting to C++ is complete.
bool CheckDependencies(base::FilePath* missing_path);

// Gets the base part of a crash report file, such as name.01234.5678.9012 from
// name.01234.5678.9012.meta or name.01234.5678.9012.log.tar.xz.  We make sure
// "name" is sanitized in CrashCollector::Sanitize to not include any periods.
// The directory part will be preserved.
base::FilePath GetBasePartOfCrashFile(const base::FilePath& file_name);

// Removes orphaned files in |crash_dir|, that are files 24 hours old or older,
// without corresponding meta file.
void RemoveOrphanedCrashFiles(const base::FilePath& crash_dir);

// Returns true and reports the reason, if report files associated with the
// given meta file should be removed.
bool ShouldRemove(const base::FilePath& meta_file, std::string* reason);

// Removes invalid files in |crash_dir|, that are unknown, corrupted, or invalid
// in other ways.
void RemoveInvalidCrashFiles(const base::FilePath& crash_dir);

// Removes report files associated with the given meta file.
// More specifically, if "foo.meta" is given, "foo.*" will be removed.
void RemoveReportFiles(const base::FilePath& meta_file);

// Returns the list of meta data files (files with ".meta" suffix), sorted by
// the timestamp in the old-to-new order.
std::vector<base::FilePath> GetMetaFiles(const base::FilePath& crash_dir);

// Gets the base name of the path pointed by |key| in the given metadata.
// Returns an empty path if the key is not found.
base::FilePath GetBaseNameFromMetadata(const brillo::KeyValueStore& metadata,
                                       const std::string& key);

// Returns which kind of report from the given payload path. Returns an empty
// string if the kind is unknown.
std::string GetKindFromPayloadPath(const base::FilePath& payload_path);

// Parses |raw_metadata| into |metadata|. Keys in metadata are validated (keys
// should consist of expected characters). Returns true on success.
bool ParseMetadata(const std::string& raw_metadata,
                   brillo::KeyValueStore* metadata);

// A helper class for sending crashes. The behaviors can be customized with
// Options class for unit testing.
class Sender {
 public:
  struct Options {
    // The shell script used for sending crashes.
    base::FilePath shell_script = base::FilePath("/sbin/crash_sender.sh");

    // Session manager client for locating the user-specific crash directories.
    org::chromium::SessionManagerInterfaceProxyInterface* proxy = nullptr;
  };

  explicit Sender(const Options& options);

  // Initializes the sender object. Returns true on success.
  bool Init();

  // Sends crashes in |crash_dir|. Returns true on if sending is successful, or
  // |crash_dir| does not exist.
  bool SendCrashes(const base::FilePath& crash_dir);

  // Sends the user-specific crashes. Returns true on if all the user-specific
  // crash directories are handled successfully.
  bool SendUserCrashes();

  // Returns the temporary directory used in the object. Valid after Init() is
  // completed successfully.
  const base::FilePath& temp_dir() const { return scoped_temp_dir_.GetPath(); }

 private:
  const base::FilePath shell_script_;
  std::unique_ptr<org::chromium::SessionManagerInterfaceProxyInterface> proxy_;
  base::ScopedTempDir scoped_temp_dir_;

  DISALLOW_COPY_AND_ASSIGN(Sender);
};

}  // namespace util

#endif  // CRASH_REPORTER_CRASH_SENDER_UTIL_H_
