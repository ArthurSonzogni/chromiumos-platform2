// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_METRICS_H_
#define MODEMFWD_METRICS_H_

#include <map>
#include <memory>
#include <string>
#include <utility>

#include <brillo/errors/error.h>
#include <metrics/metrics_library.h>

namespace modemfwd {

namespace metrics {
extern const char kMetricDlcInstallResult[];
extern const char kMetricDlcUninstallResult[];
extern const char kMetricFwUpdateLocation[];
extern const char kMetricFwInstallResult[];

// IMPORTANT: Please read this before making any changes to the file:
// - Never change existing numerical values on the enums, because the same
// numbering is used in the UMA website. If you don't need a value, comment out
// the value that is no longer needed, and remove it from the map in metrics.cc;
// this will let the error fall into the |kUnknownError| bucket.
// - Never reuse a number.
// - When adding a new value, a new entry has to be added in
// chromium/src/tools/metrics/histograms/enums.xml
enum class DlcInstallResult {
  kUnknownError = 0,
  kSuccess = 1,
  kDlcServiceReturnedInvalidDlc = 2,
  kDlcServiceReturnedAllocation = 3,
  kDlcServiceReturnedNoImageFound = 4,
  kDlcServiceReturnedNeedReboot = 5,
  kDlcServiceReturnedBusy = 6,
  kFailedUnexpectedDlcState = 7,
  kFailedTimeoutWaitingForDlcService = 8,
  kFailedTimeoutWaitingForDlcInstall = 9,
  kFailedTimeoutWaitingForInstalledState = 10,
  kDlcServiceReturnedErrorOnInstall = 11,
  kDlcServiceReturnedErrorOnGetDlcState = 12,
  kUnexpectedEmptyDlcId = 13,
  kNumConstants
};

enum class DlcUninstallResult {
  kUnknownError = 0,
  kSuccess = 1,
  kDlcServiceReturnedInvalidDlc = 2,
  kDlcServiceReturnedAllocation = 3,
  kDlcServiceReturnedNoImageFound = 4,
  kDlcServiceReturnedNeedReboot = 5,
  kDlcServiceReturnedBusy = 6,
  kDlcServiceReturnedErrorOnGetExistingDlcs = 7,
  kDlcServiceReturnedErrorOnPurge = 8,
  kUnexpectedEmptyVariant = 9,
  kNumConstants
};

enum class FwUpdateLocation {
  kRootFS = 0,
  kDlc = 1,
  kFallbackToRootFS = 2,
  kNumConstants
};

enum class FwInstallResult {
  kUnknownError = 0,
  kSuccess = 1,
  kInitFailure = 2,
  kInitManifestFailure = 3,
  kFailedToPrepareFirmwareFile = 4,
  kFlashFailure = 5,
  kFailureReturnedByHelper = 6,
  kInitJournalFailure = 7,
  kNumConstants
};

}  // namespace metrics

// Performs UMA metrics logging for the modemfw daemon.
class Metrics {
 public:
  explicit Metrics(std::unique_ptr<MetricsLibraryInterface> metrics_library)
      : metrics_library_(std::move(metrics_library)) {}

  virtual ~Metrics() = default;

  // Initializes the class.
  void Init();

  // Sends the |DlcInstallResult::kSuccess| value.
  void SendDlcInstallResultSuccess();

  // Sends the |DlcInstallResult| value that corresponds to |err|.
  void SendDlcInstallResultFailure(const brillo::Error* err);

  // Sends the |DlcUninstallResult::kSuccess| value.
  void SendDlcUninstallResultSuccess();

  // Sends the |DlcUninstallResult| value that corresponds to |err|.
  void SendDlcUninstallResultFailure(const brillo::Error* err);

  // Sends the |FwUpdateLocation| value.
  virtual void SendFwUpdateLocation(metrics::FwUpdateLocation location);

  // Sends the |FwInstallResult::kSuccess| value.
  void SendFwInstallResultSuccess();

  // Sends the |FwInstallResult| value that corresponds to |err|.
  void SendFwInstallResultFailure(const brillo::Error* err);

 protected:
  // For testing.
  Metrics() = default;
  // Sends the value for |DlcInstallResult|.
  virtual void SendDlcInstallResult(metrics::DlcInstallResult result);

  // Sends the value for |DlcUninstallResult|.
  virtual void SendDlcUninstallResult(metrics::DlcUninstallResult result);

  // Sends the value for |FwInstallResult|.
  virtual void SendFwInstallResult(metrics::FwInstallResult result);

 private:
  std::unique_ptr<MetricsLibraryInterface> metrics_library_;
  // Map DBus error codes and |modemfwd::error|s to |DlcInstallResult| values.
  typedef std::map<std::string, metrics::DlcInstallResult> DlcInstallResultMap;
  static DlcInstallResultMap install_result_;
  // Map DBus error codes and |modemfwd::error|s to |DlcUninstallResult| values.
  typedef std::map<std::string, metrics::DlcUninstallResult>
      DlcUninstallResultMap;
  static DlcUninstallResultMap uninstall_result_;
  // Map error codes to |FwInstallResult| values.
  typedef std::map<std::string, metrics::FwInstallResult> FwInstallResultMap;
  static FwInstallResultMap fw_install_result_;

  Metrics(const Metrics&) = delete;
  Metrics& operator=(const Metrics&) = delete;
};

}  // namespace modemfwd

#endif  // MODEMFWD_METRICS_H_
