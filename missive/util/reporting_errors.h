// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_UTIL_REPORTING_ERRORS_H_
#define MISSIVE_UTIL_REPORTING_ERRORS_H_
namespace reporting {

constexpr char kUmaUnavailableErrorReason[] =
    "Platform.Missive.UnavailableErrorReason";

constexpr char kUmaDataLossErrorReason[] =
    "Platform.Missive.DataLossErrorReason";

// These enum values represent the different error messages associated with
// usages of `error::UNAVAILABLE` in Missive. Anytime `error::UNAVAILABLE` is
// used, it should be UMA logged using this enum and
// kUmaUnavailableErrorReason.
//
// Update UnavailableErrorReason in
// tools/metrics/histograms/metadata/platform/enums.xml when adding new values.
//
// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class UnavailableErrorReason {
  CANNOT_CANCEL_A_JOB_THATS_ALREADY_RUNNING = 0,
  CANNOT_SCHEDULE_A_JOB_THATS_ALREADY_RUNNING = 1,
  CHROME_NOT_RESPONDING = 2,
  CLIENT_NOT_CONNECTED_TO_MISSIVE = 3,
  DAEMON_STILL_STARTING = 4,
  DBUS_ADAPTER_DESTRUCTED = 5,
  FAILED_TO_CREATE_STORAGE_QUEUE_DIRECTORY = 6,
  FILE_NOT_OPEN = 7,
  MISSIVE_CLIENT_NO_DBUS_RESPONSE = 8,
  MISSIVE_HAS_BEEN_SHUT_DOWN = 9,
  NO_DBUS_RESPONSE = 10,
  REPORT_QUEUE_DESTRUCTED = 11,
  REPORT_QUEUE_PROVIDER_DESTRUCTED = 12,
  STORAGE_DIRECTORY_DOESNT_EXIST = 13,
  STORAGE_OBJECT_DOESNT_EXIST = 14,
  STORAGE_QUEUE_SHUTDOWN = 15,
  UNABLE_TO_BUILD_REPORT_QUEUE = 16,
  UPLOAD_CLIENT_DESTRUCTED = 17,
  UPLOAD_CLIENT_NO_DBUS_RESPONSE = 18,
  MAX_VALUE = 19
};

// These enum values represent the different error messages associated with
// usages of `error::DATA_LOSS` in Missive. Anytime `error::DATA_LOSS` is
// used, it should be UMA logged using this enum and
// kUmaDataLossErrorReason.
//
// Update DataLossErrorReason in
// tools/metrics/histograms/metadata/platform/enums.xml when adding new values.
//
// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class DataLossErrorReason {
  FAILED_TO_CREATE_ENCRYPTION_KEY = 0,
  FAILED_TO_ENUMERATE_STORAGE_QUEUE_DIRECTORY = 1,
  FAILED_TO_GET_SIZE_OF_FILE = 2,
  FAILED_TO_OPEN_FILE = 3,
  FAILED_TO_OPEN_KEY_FILE = 4,
  FAILED_TO_OPEN_STORAGE_QUEUE_FILE = 5,
  FAILED_TO_PARSE_GENERATION_ID = 6,
  FAILED_TO_PARSE_RECORD = 7,
  FAILED_TO_READ_FILE = 8,
  FAILED_TO_READ_FILE_INFO = 9,
  FAILED_TO_READ_HEALTH_DATA = 10,
  FAILED_TO_READ_HEALTH_DATA_FILE_INFO = 11,
  FAILED_TO_READ_METADATA = 12,
  FAILED_TO_RESTORE_LAST_RECORD_DIGEST = 13,
  FAILED_TO_SERIALIZE_ENCRYPTED_RECORD = 14,
  FAILED_TO_SERIALIZE_KEY = 15,
  FAILED_TO_SERIALIZE_WRAPPED_RECORD = 16,
  FAILED_TO_WRITE_FILE = 17,
  FAILED_TO_WRITE_HEALTH_DATA = 18,
  FAILED_TO_WRITE_KEY_FILE = 19,
  FAILED_TO_WRITE_METADATA = 20,
  INVALID_GENERATION_GUID = 21,
  INVALID_GENERATION_ID = 22,
  METADATA_GENERATION_ID_MISMATCH = 23,
  METADATA_GENERATION_ID_OUT_OF_RANGE = 24,
  METADATA_LAST_RECORD_DIGEST_IS_CORRUPT = 25,
  MISSING_GENERATION_ID = 26,
  MAX_VALUE
};
}  // namespace reporting

#endif  // MISSIVE_UTIL_REPORTING_ERRORS_H_
