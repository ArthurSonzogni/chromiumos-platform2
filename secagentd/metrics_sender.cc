// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secagentd/metrics_sender.h"

#include <memory>
#include <utility>

#include "base/no_destructor.h"
#include "metrics/metrics_library.h"

namespace secagentd {

MetricsSender& MetricsSender::GetInstance() {
  static base::NoDestructor<MetricsSender> instance;
  return *instance;
}

MetricsSender::MetricsSender()
    : MetricsSender(std::make_unique<MetricsLibrary>()) {}

MetricsSender::MetricsSender(
    std::unique_ptr<MetricsLibraryInterface> metrics_library)
    : metrics_library_(std::move(metrics_library)) {}

}  // namespace secagentd
