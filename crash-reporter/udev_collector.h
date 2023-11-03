// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The udev collector grabs coredumps from hardware devices.
//
// For the most part, this only collects information on developer images (since
// device coredumps could include information we don't want to upload).
// However, it does grab wifi chip dumps and put them in a /var/log to be
// uploaded with feedback reports, but does NOT upload them with crash reports.
//
// The udev collector is invoked automatically by the udev rules in
// 99-crash-reporter.rules when certain classes of devices have errors.

#ifndef CRASH_REPORTER_UDEV_COLLECTOR_H_
#define CRASH_REPORTER_UDEV_COLLECTOR_H_

#include <map>
#include <memory>
#include <optional>
#include <string>

#include <base/files/file_path.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <metrics/metrics_library.h>
#include <session_manager/dbus-proxies.h>

#include "crash-reporter/connectivity_util.h"
#include "crash-reporter/crash_collector.h"

// Udev crash collector.
class UdevCollector : public CrashCollector {
 public:
  explicit UdevCollector(
      const scoped_refptr<
          base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>&
          metrics_lib);
  UdevCollector(const UdevCollector&) = delete;
  UdevCollector& operator=(const UdevCollector&) = delete;

  ~UdevCollector() override;

  // Returns the severity level and product group of the crash.
  CrashCollector::ComputedCrashSeverity ComputeSeverity(
      const std::string& exec_name) override;

  // The udev event string should be formatted as follows:
  //   "ACTION=[action]:KERNEL=[name]:SUBSYSTEM=[subsystem]"
  // The values don't have to be in any particular order. One or more of them
  // could be omitted, in which case it would be treated as a wildcard (*).
  bool HandleCrash(const std::string& udev_event);

  // This function is to be called from unit tests to specifically enable
  // connectivity fwdump feature for unit test.
  void EnableConnectivityFwdumpForTest(bool status) {
    connectivity_fwdump_feature_enabled_ = status;
  }

  // This function checks if connectivity fwdump is allowed via finch flag
  // or not. This is fail safe mechanism and if any regression observed,
  // the fwdump feature will be disabled by fbpreprocessord.
  bool CheckConnectivityFwdumpAllowedFinchFlagStatus();

  static CollectorInfo GetHandlerInfo(
      const std::string& udev_event,
      const scoped_refptr<
          base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>&
          metrics_lib);

 protected:
  std::string dev_coredump_directory_;

 private:
  friend class UdevCollectorTest;

  // Is this a "safe" device coredump, from an allowlist of driver names
  // for devices whose device coredump does not contain PII?
  bool IsSafeDevCoredump(std::map<std::string, std::string> udev_event_map);

  // This function checks if the generated coredump belongs to intel
  // wifi subdomain and returns true or false accordingly. This function
  // is called within HandleCrash() before attempting to collect
  // connectivity wifi fwdump because collection of connectivity fwdump
  // requires fetching user policy and connectivity storage path in
  // fbpreprocessord cryptohome directory. Crash-reporter should not be
  // performing all the above if the fwdump does not belong to
  // connectivity domain.
  bool IsConnectivityWiFiFwdump(int instance_number);

  // This function checks if connectivity fwdump is allowed and returns a
  // user session on which connectivity fwdump. If not allowed, the function
  // returns std::nullopt. This return value is later used to collect
  // connectivity fwdumps.
  std::optional<connectivity_util::Session>
  ConnectivityFwdumpAllowedForUserSession(
      std::map<std::string, std::string>& udev_event_map,
      int instance_number,
      const base::FilePath& coredump_path);

  // For connectivity fwdumps, we want to store in fbpreprocessord's
  // daemon-store directory and thus need to generate a customized storage path
  // with this function. The path for connectivity fw dump is different than
  // general fw dumps is because, unlike regular fwdumps we want to upload
  // connectivity fwdumps only with feedback reports.
  std::optional<base::FilePath> GetConnectivityFwdumpStoragePath(
      const connectivity_util::Session& primary_session);

  // Process udev crash logs, collecting log files according to the config
  // file (crash_reporter_logs.conf).
  bool ProcessUdevCrashLogs(const base::FilePath& crash_directory,
                            const std::string& action,
                            const std::string& kernel,
                            const std::string& subsystem);
  // Process device coredump, collecting device coredump file.
  // |instance_number| is the kernel number of the virtual device for the device
  // coredump instance.
  bool ProcessDevCoredump(const base::FilePath& crash_directory,
                          int instance_number);
  // Copy bluetooth device coredump file to crash directory, and perform
  // necessary coredump file management.
  bool AppendBluetoothCoredump(const base::FilePath& crash_directory,
                               const base::FilePath& coredump_path,
                               int instance_number);
  // Copy device coredump file to crash directory, and perform necessary
  // coredump file management.
  bool AppendDevCoredump(const base::FilePath& crash_directory,
                         const base::FilePath& coredump_path,
                         int instance_number);
  // Clear the device coredump file by performing a dummy write to it.
  bool ClearDevCoredump(const base::FilePath& coredump_path);
  // Generate the driver path of the failing device from instance and sub-path.
  base::FilePath GetFailingDeviceDriverPath(int instance_number,
                                            const std::string& sub_path);
  // Get the driver name of the failing device from uevent path.
  std::string ExtractFailingDeviceDriverName(
      const base::FilePath& failing_uevent_path);
  // Return the driver name of the device that generates the coredump.
  std::string GetFailingDeviceDriverName(int instance_number);

  // This variable is set false by default and set to true from
  // unit test to invoke connectivity fwdump feature for testing.
  // This is a temporary variable to block fwdump in FR feature until
  // policy to control this feature is available. This will be removed
  // when the feature is fully ready. TODO(b/291344512): Remove this
  // flag support once fwdump feature is fully ready.
  bool connectivity_fwdump_feature_enabled_ = false;
};

#endif  // CRASH_REPORTER_UDEV_COLLECTOR_H_
