// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_METRICS_SENDER_H_
#define SECAGENTD_METRICS_SENDER_H_

#include <memory>
#include <utility>

#include "base/memory/ptr_util.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "metrics/metrics_library.h"

namespace secagentd {
namespace testing {
class MetricsSenderTestFixture;
}  // namespace testing
namespace metrics {

static constexpr auto kMetricNamePrefix = "ChromeOS.Secagentd.";

template <class E>
struct EnumMetric {
  const char* name;
  using Enum = E;
};

enum class Policy {
  kChecked,
  kEnabled,
  kMaxValue = kEnabled,
};

static constexpr EnumMetric<Policy> kPolicy = {.name = "Policy"};

enum class BpfAttachResult {
  kSuccess,
  kErrorOpen,
  kErrorLoad,
  kErrorAttach,
  kErrorRingBuffer,
  kMaxValue = kErrorRingBuffer,
};

static constexpr EnumMetric<BpfAttachResult> kProcessBpfAttach = {
    .name = "Bpf.Process.AttachResult"};

}  // namespace metrics

// Class for sending UMA metrics. Expected to be accessed as a Singleton via
// MetricsSender::GetInstance().
class MetricsSender {
 public:
  static MetricsSender& GetInstance();

  // Send a metrics::EnumMetric sample to UMA. Synchronously calls into
  // MetricsLibrary.
  // Warning: Not safe for use in hot paths. Limit usage to infrequent events
  // (such as during daemon initialization).
  template <typename M>
  bool SendEnumMetricToUMA(M metric, typename M::Enum sample) {
    return metrics_library_->SendEnumToUMA(
        base::StrCat({metrics::kMetricNamePrefix, metric.name}), sample);
  }

  void SetMetricsLibraryForTesting(
      std::unique_ptr<MetricsLibraryInterface> metrics_library) {
    metrics_library_ = std::move(metrics_library);
  }

 private:
  friend class base::NoDestructor<MetricsSender>;
  friend class testing::MetricsSenderTestFixture;

  // Allow calling the private test-only constructor without befriending
  // unique_ptr.
  template <typename... Args>
  static std::unique_ptr<MetricsSender> CreateForTesting(Args&&... args) {
    return base::WrapUnique(new MetricsSender(std::forward<Args>(args)...));
  }

  MetricsSender();
  explicit MetricsSender(
      std::unique_ptr<MetricsLibraryInterface> metrics_library);

  std::unique_ptr<MetricsLibraryInterface> metrics_library_;
};
}  // namespace secagentd

#endif  // SECAGENTD_METRICS_SENDER_H_
