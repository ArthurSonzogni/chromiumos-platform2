// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLP_DLP_METRICS_H_
#define DLP_DLP_METRICS_H_

#include <memory>
#include <metrics/metrics_library.h>
#include <string>

namespace dlp {

constexpr char kDlpFanotifyErrorHistogram[] = "Enterprise.Dlp.Errors.Fanotify";
constexpr char kDlpFanotifyDeleteEventSupport[] =
    "Enterprise.Dlp.FanotifyDeleteEventSupport";
constexpr char kDlpFanotifyMarkFilesystemSupport[] =
    "Enterprise.Dlp.FanotifyMarkFilesystemSupport";

// Type of errors triggered by fanotify usage in the DLP daemon.
enum class FanotifyError {
  kUnknownError = 0,
  // Error when executing fanotify_mark.
  kMarkError = 1,
  // Error when executing select in FanotifyReaderThread.
  kSelectError = 2,
  // Error when executing ioctl in FanotifyReaderThread.
  kIoctlError = 3,
  // Error when executing fd in FanotifyReaderThread.
  kFdError = 4,
  // Error triggered when there is a mismatch of fanotify metadata version.
  kMetadataMismatchError = 5,
  // Error when executing fstat in FanotifyReaderThread.
  kFstatError = 6,
  // Error triggered when receiving an invalid file descriptor.
  kInvalidFileDescriptorError = 7,
  // Error triggered when receiving an unexpected file handle type.
  kUnexpectedFileHandleTypeError = 8,
  // Error triggered when receiving an unexpected event info type.
  kUnexpectedEventInfoTypeError = 9,
  // Error during initialization.
  kInitError = 10,
  // For SendEnumToUMA() usage.
  kMaxValue = kInitError,
};

// Sends UMAs related to the DLP daemon.
class DlpMetrics {
 public:
  DlpMetrics();
  ~DlpMetrics();

  // Send a boolean to UMA.
  void SendBooleanHistogram(const std::string& name, bool value) const;

  // Records whether there's an error happening when using fanotify.
  void SendFanotifyError(FanotifyError error) const;

 private:
  std::unique_ptr<MetricsLibraryInterface> metrics_lib_;
};

}  // namespace dlp

#endif  // DLP_DLP_METRICS_H_
