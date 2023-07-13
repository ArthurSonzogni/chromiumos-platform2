// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The GSC collector runs just after boot and grabs information about crashes in
// the Google Security Chip from `gsctool`.
// The GSC collector runs via the crash-boot-collect service.

#ifndef CRASH_REPORTER_GSC_COLLECTOR_H_
#define CRASH_REPORTER_GSC_COLLECTOR_H_

#include <memory>
#include <string>

#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <metrics/metrics_library.h>

#include "crash-reporter/gsc_collector_base.h"

// GSC crash collector.
class GscCollector : public GscCollectorBase {
 public:
  explicit GscCollector(
      const scoped_refptr<
          base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>&
          metrics_lib);
  GscCollector(const GscCollector&) = delete;
  GscCollector& operator=(const GscCollector&) = delete;

  ~GscCollector() override = default;

 private:
  GscCollectorBase::Status GetTi50Flog(std::string* flog_output);

  Status GetGscFlog(std::string* flog_output) override;
};

#endif  // CRASH_REPORTER_GSC_COLLECTOR_H_
