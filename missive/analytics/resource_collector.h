// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_ANALYTICS_RESOURCE_COLLECTOR_H_
#define MISSIVE_ANALYTICS_RESOURCE_COLLECTOR_H_

#include <memory>

#include <base/time/time.h>
#include <base/timer/timer.h>
#include <gtest/gtest_prod.h>
#include <metrics/metrics_library.h>

namespace reporting::analytics {

class ResourceCollector {
 public:
  explicit ResourceCollector(base::TimeDelta interval);
  ResourceCollector(const ResourceCollector&) = delete;
  ResourceCollector& operator=(const ResourceCollector&) = delete;
  virtual ~ResourceCollector();

 protected:
  // The ChromeOS metrics instance.
  std::unique_ptr<MetricsLibraryInterface> metrics_{
      std::make_unique<MetricsLibrary>()};

 private:
  friend class ResourceCollectorTest;

  // collect resource usage info and send it to the UMA Chrome client.
  // Log any errors but ignore them.
  virtual void Collect() = 0;

  // Timer for executing the resource usage collection task.
  base::RepeatingTimer timer_;
};

}  // namespace reporting::analytics

#endif  // MISSIVE_ANALYTICS_RESOURCE_COLLECTOR_H_
