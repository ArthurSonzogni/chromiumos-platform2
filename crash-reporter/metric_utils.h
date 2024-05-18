// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_METRIC_UTILS_H_
#define CRASH_REPORTER_METRIC_UTILS_H_

#include "crash-reporter/crash_collection_status.h"

enum class CrashReporterCollector;
enum class CrashSendingMode;

namespace util {
class BrilloProcessFactory;
};

// On destruction, records in CrosEvents that a CrashCollector has finished.
// Returned by RecordCrashReporterStart so that we always pair start / status
// events. The caller of RecordCrashReporterStart() should call |set_status|
// before the recorder is destroyed.
class CrashReporterStatusRecorder {
 public:
  // Don't copy, messes up the start/status pairing.
  CrashReporterStatusRecorder(const CrashReporterStatusRecorder&) = delete;
  CrashReporterStatusRecorder& operator=(const CrashReporterStatusRecorder&) =
      delete;

  // Actually records the metric on destruction.
  ~CrashReporterStatusRecorder();

  void set_status(CrashCollectionStatus status) { status_ = status; }

  CrashCollectionStatus status() const { return status_; }

 private:
  friend CrashReporterStatusRecorder RecordCrashReporterStart(
      CrashReporterCollector collector, CrashSendingMode crash_sending_mode);

  // Only created by RecordCrashReporterStart()
  CrashReporterStatusRecorder(CrashReporterCollector collector,
                              CrashSendingMode crash_sending_mode);
  const CrashReporterCollector collector_;
  const CrashSendingMode crash_sending_mode_;
  CrashCollectionStatus status_ = CrashCollectionStatus::kUnknownStatus;
};

// Record in CrosEvents that a CrashCollector has started collecting a crash.
// This returns a CrashReporterStatusRecorder which record the status (end)
// event; the caller should call set_status() before the returned recorder is
// destroyed. See metrics/structured/sync/structured.xml for more.
[[nodiscard]] CrashReporterStatusRecorder RecordCrashReporterStart(
    CrashReporterCollector collector, CrashSendingMode crash_sending_mode);

// Override the BrilloProcessFactory used to create subprocesses. For testing.
// Call with nullptr to reset to the default BrilloProcessFactory. Caller
// retains ownership of the factory and should call
// OverrideBrilloProcessFactoryForTesting(nullptr) before the factory object is
// deleted.
void OverrideBrilloProcessFactoryForTesting(
    util::BrilloProcessFactory* factory);

#endif  // CRASH_REPORTER_METRIC_UTILS_H_
