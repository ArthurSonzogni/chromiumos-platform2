// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_CLOBBER_STATE_COLLECTOR_H_
#define CRASH_REPORTER_CLOBBER_STATE_COLLECTOR_H_

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <metrics/metrics_library.h>

#include "crash-reporter/crash_collector.h"

inline constexpr const char kNoErrorLogged[] = "No error logged.";

// Collect clobber.log which has the error messages that led to the stateful
// clobber.
class ClobberStateCollector : public CrashCollector {
 public:
  explicit ClobberStateCollector(
      const scoped_refptr<
          base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>&
          metrics_lib);
  ClobberStateCollector(const ClobberStateCollector&) = delete;
  ClobberStateCollector& operator=(const ClobberStateCollector&) = delete;

  ~ClobberStateCollector() override = default;

  bool Collect();

  static CollectorInfo GetHandlerInfo(
      bool clobber_state,
      const scoped_refptr<
          base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>&
          metrics_lib);

 protected:
  base::FilePath tmpfiles_log_;
};

#endif  // CRASH_REPORTER_CLOBBER_STATE_COLLECTOR_H_
