// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_CRASH_SENDER_UTIL_H_
#define CRASH_REPORTER_CRASH_SENDER_UTIL_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/optional.h>
#include <base/time/clock.h>
#include <base/time/time.h>
#include <brillo/http/http_form_data.h>
#include <brillo/key_value_store.h>
#include <brillo/osrelease_reader.h>
#include <metrics/metrics_library.h>
#include <session_manager/dbus-proxies.h>
#include <shill/dbus-proxies.h>

namespace util {

// Maximum crashes to send per 24 hours.
constexpr int kMaxCrashRate = 32;

// Maximum time to wait for ensuring a meta file is complete.
constexpr base::TimeDelta kMaxHoldOffTime = base::TimeDelta::FromSeconds(30);

// Maximum time to sleep before attempting to send a crash report. This value is
// inclusive as an upper bound, thus 0 means a crash report can be sent
// immediately.
constexpr int kMaxSpreadTimeInSeconds = 600;

// Parsed command line flags.
struct CommandLineFlags {
  base::TimeDelta max_spread_time;
  std::string crash_directory;
  bool ignore_rate_limits = false;
  bool ignore_hold_off_time = false;
  bool allow_dev_sending = false;
  bool ignore_pause_file = false;
  bool test_mode = false;
};

// Crash information obtained in ChooseAction().
struct CrashInfo {
  brillo::KeyValueStore metadata;
  base::FilePath payload_file;
  std::string payload_kind;
  // Last modification time of the associated .meta file
  base::Time last_modified;
};

// Details of a crash report. Contains more information than CrashInfo, as
// additional information is extracted at a stage later stage.
struct CrashDetails {
  base::FilePath meta_file;
  base::FilePath payload_file;
  std::string payload_kind;
  std::string client_id;
  const brillo::KeyValueStore& metadata;
};

// Represents a metadata file name, and its parsed metadata.
typedef std::pair<base::FilePath, CrashInfo> MetaFile;

// Actions returned by ChooseAction().
enum Action {
  kRemove,  // Should remove the crash report.
  kIgnore,  // Should ignore (keep) the crash report.
  kSend,    // Should send the crash report.
};

// Parses the command line, and handles the command line flags.
//
// On error, the process exits as a failure with an error message for the
// first-encountered error.
void ParseCommandLine(int argc,
                      const char* const* argv,
                      CommandLineFlags* flags);

// Records that the crash sending is done.
void RecordCrashDone();

// Returns true if mock is enabled.
bool IsMock();

// Returns true if mock is enabled and we should succeed.
bool IsMockSuccessful();

// Returns true if the marker file exists indicating we should pause sending.
// This can be overridden with a command line flag to the program.
bool DoesPauseFileExist();

// Returns the string that describes the type of image. Returns an empty string
// if we shouldn't specify the image type.
std::string GetImageType();

// Gets the base part of a crash report file, such as name.01234.5678.9012 from
// name.01234.5678.9012.meta or name.01234.5678.9012.log.tar.xz.  We make sure
// "name" is sanitized in CrashCollector::Sanitize to not include any periods.
// The directory part will be preserved.
base::FilePath GetBasePartOfCrashFile(const base::FilePath& file_name);

// Removes orphaned files in |crash_dir|, that are files 24 hours old or older,
// without corresponding meta file.
void RemoveOrphanedCrashFiles(const base::FilePath& crash_dir);

// Chooses an action to take for the crash report associated with the given meta
// file, and reports the reason. |metrics_lib| is used to check if metrics are
// enabled, etc. The crash information will be stored in |info| for reuse.
// Setting |allow_dev_sending| to true allows sending of crash reports even for
// non-official builds.
Action ChooseAction(const base::FilePath& meta_file,
                    MetricsLibraryInterface* metrics_lib,
                    bool allow_dev_sending,
                    std::string* reason,
                    CrashInfo* info);

// Sort the vector of crash reports so that the report we want to send first
// is at the front of the vector.
void SortReports(std::vector<MetaFile>* reports);

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
// The original contents of |metadata| will be lost.
bool ParseMetadata(const std::string& raw_metadata,
                   brillo::KeyValueStore* metadata);

// Returns true if the metadata is complete.
bool IsCompleteMetadata(const brillo::KeyValueStore& metadata);

// Returns true if the given timestamp file is new enough, indicating that there
// was a recent attempt to send a crash report.
bool IsTimestampNewEnough(const base::FilePath& timestamp_file);

// Returns true if sending a crash report now does not exceed |max_crash_rate|
// per 24 hours.
//
// This function checks/creates/removes timestamp files in |timestamps_dir| to
// track how many attempts were made to send crash reports in that past 24
// hours.
bool IsBelowRate(const base::FilePath& timestamps_dir, int max_crash_rate);

// Computes a sleep time needed before attempting to send a new crash report.
// On success, returns true and stores the result in |sleep_time|. On error,
// returns false.
bool GetSleepTime(const base::FilePath& meta_file,
                  const base::TimeDelta& max_spread_time,
                  const base::TimeDelta& hold_off_time,
                  base::TimeDelta* sleep_time);

// Gets the client ID if it exists, otherwise it generates it, saves it and
// returns that new ID. If it is unable to create the directory for storage, the
// empty string is returned.
std::string GetClientId();

// A helper class for sending crashes. The behaviors can be customized with
// Options class for unit testing.
//
// Crash reports will be sent even when the device is on a mobile data
// connection (see crbug.com/185110 for discussion).
class Sender {
 public:
  struct Options {
    // Session manager client for locating the user-specific crash directories.
    org::chromium::SessionManagerInterfaceProxyInterface*
        session_manager_proxy = nullptr;

    // Shill FlimFlam Manager proxy interface for determining network state.
    org::chromium::flimflam::ManagerProxyInterface* shill_proxy = nullptr;

    // Maximum crashes to send per 24 hours.
    int max_crash_rate = kMaxCrashRate;

    // Maximum time to sleep before attempting to send.
    base::TimeDelta max_spread_time;

    // Do not send the crash report until the meta file is at least this old.
    // This avoids problems with crash reports being sent out while they are
    // still being written.
    base::TimeDelta hold_off_time = kMaxHoldOffTime;

    // Alternate sleep function for unit testing.
    base::Callback<void(base::TimeDelta)> sleep_function;

    // Boundary to use in the form data.
    std::string form_data_boundary;

    // If true, we will ignore other checks when deciding if we should write to
    // the Chrome uploads.log file.
    bool always_write_uploads_log = false;

    // If true, we allow sending crash reports for unofficial test images and
    // the reports are uploaded to a staging crash server instead.
    bool allow_dev_sending = false;

    // If true, just log the kTestModeSuccessful message if the crash report
    // looks legible instead of actually uploading it.
    bool test_mode = false;
  };

  Sender(std::unique_ptr<MetricsLibraryInterface> metrics_lib,
         std::unique_ptr<base::Clock> clock,
         const Options& options);

  // Initializes the sender object. Returns true on success.
  bool Init();

  // Lock the lock file so no other instance of crash_sender can access the
  // disk files. Dies if lock file cannot be acquired after a delay.
  //
  // Returns the File object holding the lock.
  base::File AcquireLockFileOrDie();

  // Removes invalid files in |crash_dir|, that are unknown, corrupted, or
  // invalid in other ways, and picks crash reports that should be sent to the
  // server. The meta files of the latter will be stored in |to_send|.
  void RemoveAndPickCrashFiles(const base::FilePath& directory,
                               std::vector<MetaFile>* reports_to_send);

  // Sends each crash in |crash_meta_files|, in multiple steps:
  //
  // For each meta file:
  // - Sleeps to avoid overloading the network
  // - Checks if the device enters guest mode, and stops if entered.
  // - Enforces the rate limit per 24 hours.
  // - Removes crash files that are successfully uploaded.
  void SendCrashes(const std::vector<MetaFile>& crash_meta_files);

  // Get a list of all directories that might hold user-specific crashes.
  std::vector<base::FilePath> GetUserCrashDirectories();

  // Given the |details| for a crash, creates a brillo::http::FormData object
  // which will have all of the fields for submission to the crash server
  // populated. Returns a nullptr if there were critical errors in populating
  // the data. This also logs out all of the details during the process. On
  // success, |product_name_out| is also set to the product name (it's not
  // possible to extract data from the returned FormData object in a
  // non-destructive manner).
  std::unique_ptr<brillo::http::FormData> CreateCrashFormData(
      const CrashDetails& details, std::string* product_name_out);

  // Returns the temporary directory used in the object. Valid after Init() is
  // completed successfully.
  const base::FilePath& temp_dir() const { return scoped_temp_dir_.GetPath(); }

 private:
  friend class IsNetworkOnlineTest;

  // Requests to send a crash report represented with the given crash details.
  bool RequestToSendCrash(const CrashDetails& details);

  // Makes sure we have the DBus object initialized and connected.
  void EnsureDBusIsReady();

  // Looks through |keys| in the os-release data using brillo::OsReleaseReader.
  // Keys are searched in order until a value is found. Returns the value in
  // the Optional if found, otherwise the Optional is empty.
  base::Optional<std::string> GetOsReleaseValue(
      const std::vector<std::string>& keys);

  // Checks if we have an online connection state so we can try sending crash
  // reports.
  bool IsNetworkOnline();

  std::unique_ptr<MetricsLibraryInterface> metrics_lib_;
  std::unique_ptr<org::chromium::SessionManagerInterfaceProxyInterface>
      session_manager_proxy_;
  std::unique_ptr<org::chromium::flimflam::ManagerProxyInterface> shill_proxy_;
  std::vector<std::string> proxy_servers_;
  std::string form_data_boundary_;
  bool always_write_uploads_log_;
  base::ScopedTempDir scoped_temp_dir_;
  const int max_crash_rate_;
  const base::TimeDelta max_spread_time_;
  const base::TimeDelta hold_off_time_;
  base::Callback<void(base::TimeDelta)> sleep_function_;
  bool allow_dev_sending_;
  const bool test_mode_;
  std::unique_ptr<base::Clock> clock_;
  scoped_refptr<dbus::Bus> bus_;
  std::unique_ptr<brillo::OsReleaseReader> os_release_reader_;

  DISALLOW_COPY_AND_ASSIGN(Sender);
};

}  // namespace util

#endif  // CRASH_REPORTER_CRASH_SENDER_UTIL_H_
