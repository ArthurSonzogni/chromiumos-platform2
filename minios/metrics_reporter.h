// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_METRICS_REPORTER_H_
#define MINIOS_METRICS_REPORTER_H_

#include <memory>
#include <string>

#include "metrics/metrics_library.h"
#include "minios/metrics_reporter_interface.h"
#include "minios/process_manager.h"

namespace minios {

class MetricsReporter : public MetricsReporterInterface {
 public:
  explicit MetricsReporter(
      ProcessManagerInterface* process_manager,
      std::unique_ptr<MetricsLibraryInterface> metrics_lib = nullptr);
  virtual ~MetricsReporter() = default;

  MetricsReporter(const MetricsReporter&) = delete;
  MetricsReporter& operator=(const MetricsReporter&) = delete;

  void ReportNBRComplete() override;

 private:
  std::unique_ptr<MetricsLibraryInterface> metrics_lib_;
  ProcessManagerInterface* process_manager_;
};

}  // namespace minios

#endif  // MINIOS_METRICS_REPORTER_H_
