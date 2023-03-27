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

// This should always follow the missive status code.
// https://chromium.googlesource.com/chromiumos/platform2/+/6142bdcb70dc0987f9234c2294660f798d5df05a/missive/util/status.h#26
enum class SendMessage {
  kSuccess,
  kCancelled,
  kUnknown,
  kInvalidArgument,
  kDeadlineExceeded,
  kNotFound,
  kAlreadyExists,
  kPermissionDenied,
  kResourceExhausted,
  kFailedPrecondition,
  kAborted,
  kOutOfRange,
  kUnimplemetned,
  kInternal,
  kUnavailable,
  kDataLoss,
  kUnauthenticated,
  // The value should always be kept last.
  kMaxValue = kUnauthenticated,
};

static constexpr EnumMetric<SendMessage> kSendMessage = {
    .name = "SendMessageResult"};

enum class CrosBootmode {
  kSuccess,
  kValueNotSet,
  kUnavailable,
  kFailedRetrieval,
  kMaxValue = kFailedRetrieval,
};

static constexpr EnumMetric<CrosBootmode> kCrosBootmode = {.name =
                                                               "Bootmode.Cros"};

enum class UefiBootmode {
  kSuccess,
  kFileNotFound,
  kFailedToReadBootParams,
  kBootParamInvalidSize,
  kMaxValue = kBootParamInvalidSize,
};

static constexpr EnumMetric<UefiBootmode> kUefiBootmode = {.name =
                                                               "Bootmode.Uefi"};

enum class Tpm {
  kSuccess,
  kValueNotSet,
  kUnavailable,
  kFailedRetrieval,
  kMaxValue = kFailedRetrieval,
};

static constexpr EnumMetric<Tpm> kTpm = {.name = "Tpm"};

enum class Cache {
  kCacheHit,
  kCacheMiss,
  kProcfsFilled,
  kMaxValue = kProcfsFilled,
};

static constexpr EnumMetric<Cache> kCache = {.name = "Cache"};

enum class ProcessEvent {
  kFullEvent,
  kSpawnPidNotInCache,
  kProcessPidNotInCache,
  kParentPidNotInCache,
  kParentStillAlive,
  kMaxValue = kParentStillAlive,
};

static constexpr EnumMetric<ProcessEvent> kExecEvent = {
    .name = "Process.ExecEvent"};
static constexpr EnumMetric<ProcessEvent> kTerminateEvent = {
    .name = "Process.TerminateEvent"};
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
