// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/on_device_model/ml/performance_class.h"

#include <algorithm>
#include <string>

#include <base/compiler_specific.h>
#include <base/memory/raw_ref.h>
#include <base/strings/strcat.h>
#include <base/system/sys_info.h>
#include <metrics/metrics_library.h>

namespace ml {
namespace {

constexpr uint64_t kBytesPerMb = 1024 * 1024;

// The threshold for GPU RAM below which the device is considered VeryLow.
constexpr int kLowRAMThreshold = 3000;
// RAM threshold necessary to be considered High or better.
constexpr int kHighRAMThreshold = 5500;

// Output threshold to be considered Low or better.
constexpr int kLowOutputThreshold = 5;

// Input speed min thresholds or each device class.
constexpr int kLowThreshold = 50;
constexpr int kMediumThreshold = 75;
constexpr int kHighThreshold = 150;
constexpr int kVeryHighThreshold = 500;

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class VeryLowPerformanceReason {
  kLowRAM = 0,
  kSlowOutput = 1,
  kSlowInput = 2,
  kMaxValue = kSlowInput,
};

void ReportHistogramCounts10000(raw_ref<MetricsLibraryInterface> metrics,
                                std::string_view name,
                                int sample) {
  metrics->SendToUMA(std::string(name), sample, 1, 10000, 50);
}

void ReportHistogramMemoryLargeMB(raw_ref<MetricsLibraryInterface> metrics,
                                  const std::string& name,
                                  int sample) {
  metrics->SendToUMA(name, sample, 1, 64000, 100);
}

void LogVeryLowReason(raw_ref<MetricsLibraryInterface> metrics,
                      VeryLowPerformanceReason reason) {
  metrics->SendEnumToUMA("OnDeviceModel.BenchmarkVeryLowReason", reason);
}

}  // namespace

DISABLE_CFI_DLSYM
on_device_model::mojom::PerformanceClass GetEstimatedPerformanceClass(
    raw_ref<MetricsLibraryInterface> metrics, const ChromeML& chrome_ml) {
  ChromeMLPerformanceInfo info;
  bool success = chrome_ml.api().GetEstimatedPerformance(&info);
  metrics->SendBoolToUMA("OnDeviceModel.BenchmarkSuccess", success);
  if (!success) {
    return on_device_model::mojom::PerformanceClass::kError;
  }
  const float input_speed = info.input_speed;
  const float output_speed = info.output_speed;
  const bool is_integrated_gpu = info.is_integrated_gpu;

  int system_ram = base::SysInfo::AmountOfPhysicalMemoryMB();
  ReportHistogramMemoryLargeMB(
      metrics,
      base::StrCat({"OnDeviceModel.SystemRAM.",
                    is_integrated_gpu ? "Integrated" : "Discrete"}),
      system_ram);
  uint64_t device_heap_mb = info.device_heap_size / kBytesPerMb;
  ReportHistogramMemoryLargeMB(
      metrics,
      base::StrCat({"OnDeviceModel.DeviceHeapSize.",
                    is_integrated_gpu ? "Integrated" : "Discrete"}),
      device_heap_mb);
  if (info.max_buffer_size) {
    ReportHistogramMemoryLargeMB(
        metrics,
        base::StrCat({"OnDeviceModel.MaxBufferSize.",
                      is_integrated_gpu ? "Integrated" : "Discrete"}),
        info.max_buffer_size);
  }

  ReportHistogramCounts10000(
      metrics, "OnDeviceModel.BenchmarkEstimatedTokensPerSecond.Input",
      input_speed);
  ReportHistogramCounts10000(
      metrics, "OnDeviceModel.BenchmarkEstimatedTokensPerSecond.Output",
      output_speed);

  // Integrated GPUs can use at least 1/2 of system RAM as VRAM. Mac doesn't
  // allow directly querying VRAM, and instead returns the "recommended" maximum
  // VRAM to use, which may change depending on system load. This ensures that
  // for integrated GPUs we have a more reasonable value in that case.
  if (is_integrated_gpu) {
    device_heap_mb =
        std::max(static_cast<uint64_t>(system_ram / 2), device_heap_mb);
  }
  // Devices with low RAM are considered very low perf.
  if (device_heap_mb < static_cast<uint64_t>(kLowRAMThreshold)) {
    LogVeryLowReason(metrics, VeryLowPerformanceReason::kLowRAM);
    return on_device_model::mojom::PerformanceClass::kVeryLow;
  }

  // Devices that output less than 6 tk/s are considered very low perf.
  if (output_speed < kLowOutputThreshold) {
    LogVeryLowReason(metrics, VeryLowPerformanceReason::kSlowOutput);
    return on_device_model::mojom::PerformanceClass::kVeryLow;
  }
  // VeryLow:  [0, 50)
  // Low:      [50, 100)
  // Medium:   [100, 250)
  // High:     [250, 750)
  // VeryHigh: [750, inf)
  if (input_speed < kLowThreshold) {
    LogVeryLowReason(metrics, VeryLowPerformanceReason::kSlowInput);
    return on_device_model::mojom::PerformanceClass::kVeryLow;
  } else if (input_speed < kMediumThreshold) {
    return on_device_model::mojom::PerformanceClass::kLow;
  } else if (input_speed < kHighThreshold ||
             device_heap_mb < static_cast<uint64_t>(kHighRAMThreshold)) {
    return on_device_model::mojom::PerformanceClass::kMedium;
  } else if (input_speed < kVeryHighThreshold) {
    return on_device_model::mojom::PerformanceClass::kHigh;
  } else {
    return on_device_model::mojom::PerformanceClass::kVeryHigh;
  }
}

}  // namespace ml
