// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLP_DLP_METRICS_H_
#define DLP_DLP_METRICS_H_

#include <memory>
#include <metrics/metrics_library.h>
#include <string>

namespace dlp {

constexpr char kDlpFanotifyDeleteEventSupport[] =
    "Enterprise.Dlp.FanotifyDeleteEventSupport";
constexpr char kDlpFanotifyMarkFilesystemSupport[] =
    "Enterprise.Dlp.FanotifyMarkFilesystemSupport";

constexpr char kDlpFanotifyErrorHistogram[] = "Enterprise.Dlp.Errors.Fanotify";
constexpr char kDlpFileDatabaseErrorHistogram[] =
    "Enterprise.Dlp.Errors.FileDatabase";

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

// Type of errors triggered by the DLP database.
enum class DatabaseError {
  kUnknownError = 0,
  // Error when connecting to the database.
  kConnectionError = 1,
  // Error when creating a database table.
  kCreateTableError = 2,
  // Error when inserting an entry into a database table.
  kInsertIntoTableError = 3,
  // Error when querying the database.
  kQueryError = 4,
  // Error when deleting database entries.
  kDeleteError = 5,
  // Error triggered when a query returns multiple database entries for the same
  // inode.
  kMultipleEntriesForInode = 6,
  // Error while creating the database directory.
  kCreateDirError = 7,
  // Error while setting database ownership.
  kSetOwnershipError = 8,
  // For SendEnumToUMA() usage.
  kMaxValue = kSetOwnershipError,
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

  // Records whether an error occurs while executing database procedures.
  void SendDatabaseError(DatabaseError error) const;

 private:
  std::unique_ptr<MetricsLibraryInterface> metrics_lib_;
};

}  // namespace dlp

#endif  // DLP_DLP_METRICS_H_
