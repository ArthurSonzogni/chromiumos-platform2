// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Functions that translate between the CrashReporterCollector enum and the
// string representation.
#ifndef CRASH_REPORTER_CRASH_COLLECTOR_NAMES_H_
#define CRASH_REPORTER_CRASH_COLLECTOR_NAMES_H_

#include <string_view>

// Enumeration of all the CrashCollectors in crash_reporter.
// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
// Be sure to update kNameCollectorPairs if you add a new value here.
// LINT.IfChange(collector_list)
enum class CrashReporterCollector {
  kUnknownCollector = 0,
  kUser = 1,
  kChrome = 2,
  kBERT = 3,
  kClobberState = 4,
  kKernelWarning = 5,
  kCrashReporterFailure = 6,
  kEphemeral = 7,
  kGenericFailure = 8,
  kGSC = 9,
  kUdev = 10,
  kEC = 11,
  kKernel = 12,
  kMissedCrash = 13,
  kMountFailure = 14,
  kUncleanShutdown = 15,
  kSecurityAnomaly = 16,
  kSELinuxViolation = 17,
  kVm = 18,
  kArcJava = 19,
  kArcvmCxx = 20,
  kArcvmKernel = 21,
  kArcppCxx = 22,
  kMock = 23,

  kMaxValue = kMock
};
// LINT.ThenChange(crash_collector_names.cc:collector_list)

// Gets a human-readable-ish name for a collector given a
// CrashReporterCollector enum value.
const char* GetNameForCollector(CrashReporterCollector collector);

// Given the human-readable-ish name for a collector, return the enum value.
CrashReporterCollector GetCollectorForName(std::string_view collector_name);

#endif  // CRASH_REPORTER_CRASH_COLLECTOR_NAMES_H_
