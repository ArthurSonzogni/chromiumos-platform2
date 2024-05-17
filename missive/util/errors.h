// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_UTIL_ERRORS_H_
#define MISSIVE_UTIL_ERRORS_H_

namespace reporting {

constexpr char kUmaUnavailableErrorReason[] =
    "Platform.Missive.UnavailableErrorReason";

// These enum values represent the different error messages associated with
// usages of `error::UNAVAILABLE` in Missive. Anytime `error::UNAVAILABLE` is
// used, it should be UMA logged using this enum and
// kUmaUnavailableErrorReason.
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

}  // namespace reporting

#endif  // MISSIVE_UTIL_ERRORS_H_
