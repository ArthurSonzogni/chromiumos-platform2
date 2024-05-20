// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_CRASH_SENDING_MODE_H_
#define CRASH_REPORTER_CRASH_SENDING_MODE_H_

// Whether crash reporter sends crash reports by writing files to a spool
// directory or by sending them via dbus to debugd.
// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class CrashSendingMode {
  // Use the normal crash sending mode: Write crash files out to disk, and
  // assume crash_sender will be along later to send them out.
  kNormal = 0,
  // Use a special mode suitable for a login-crash-loop scenario. This happens
  // when Chrome crashes repeatedly right after login, leading to an imminent
  // user logout due to the inability to achieve a stable logged-in state. In
  // this mode, crash files are written to special in-memory locations since the
  // usual user crash directory in the cryptohome will be locked out too quickly
  // These in-memory files are then sent to debugd for immediate upload because
  // they are in volatile storage, and the user might turn off their machine
  // very quickly in frustration.
  kCrashLoop = 1,

  kMaxValue = kCrashLoop
};

#endif  // CRASH_REPORTER_CRASH_SENDING_MODE_H_
