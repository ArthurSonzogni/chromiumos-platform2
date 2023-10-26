// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_MANAGEMENT_METRICS_H_
#define DEVICE_MANAGEMENT_METRICS_H_

#include "device_management/install_attributes/install_attributes.h"

#include <metrics/metrics_library.h>

namespace device_management {

class Metrics : private MetricsLibrary {
 public:
  Metrics() = default;
  Metrics(const Metrics&) = delete;
  Metrics& operator=(const Metrics&) = delete;

  virtual ~Metrics() = default;

  virtual void ReportInstallAttributesStatus(InstallAttributes::Status status);

 private:
  MetricsLibraryInterface* metrics_library_{this};
};

}  // namespace device_management

#endif  // DEVICE_MANAGEMENT_METRICS_H_
