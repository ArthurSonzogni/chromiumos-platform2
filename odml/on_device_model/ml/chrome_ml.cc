// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/on_device_model/ml/chrome_ml.h"

#include <atomic>
#include <string>

#include <base/base_paths.h>
#include <base/check.h>
#include <base/compiler_specific.h>
#include <base/debug/crash_logging.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/memory/raw_ref.h>
#include <base/memory/ref_counted.h>
#include <base/no_destructor.h>
#include <base/path_service.h>
#include <base/process/process.h>
#include <base/synchronization/lock.h>
#include <build/build_config.h>
#include <metrics/metrics_library.h>

namespace ml {

namespace {

// The global ChromeML pointer.
std::atomic<ChromeML*> g_chrome_ml;

// Signature of the GetDawnNativeProcs() function which the shared library
// exports.
using DawnNativeProcsGetter = const DawnProcTable* (*)();

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class GpuErrorReason {
  kOther = 0,
  kDxgiErrorDeviceHung = 1,
  kDxgiErrorDeviceRemoved = 2,
  kMaxValue = kDxgiErrorDeviceRemoved,
};

// The fatal error & histogram recording functions may run on different threads,
// so we will need to lock the metrics object access.
base::NoDestructor<base::Lock> g_metrics_lock;
int g_chrome_ml_count;
MetricsLibraryInterface* g_metrics;

void FatalGpuErrorFn(const char* msg) {
  SCOPED_CRASH_KEY_STRING1024("ChromeML(GPU)", "error_msg", msg);
  std::string msg_str(msg);
  GpuErrorReason error_reason = GpuErrorReason::kOther;
  if (msg_str.find("DXGI_ERROR_DEVICE_HUNG") != std::string::npos) {
    error_reason = GpuErrorReason::kDxgiErrorDeviceHung;
  } else if (msg_str.find("DXGI_ERROR_DEVICE_REMOVED") != std::string::npos) {
    error_reason = GpuErrorReason::kDxgiErrorDeviceRemoved;
  }
  {
    base::AutoLock lock(*g_metrics_lock);
    if (g_metrics) {
      g_metrics->SendEnumToUMA("OnDeviceModel.GpuErrorReason", error_reason);
    }
  }
  if (error_reason == GpuErrorReason::kOther) {
    // Collect crash reports on unknown errors.
    CHECK(false) << "ChromeML(GPU) Error: " << msg;
  } else {
    base::Process::TerminateCurrentProcessImmediately(0);
  }
}

void FatalErrorFn(const char* msg) {
  SCOPED_CRASH_KEY_STRING1024("ChromeML", "error_msg", msg);
  CHECK(false) << "ChromeML Error: " << msg;
}

// Helpers to disabiguate overloads in base.
void RecordExactLinearHistogram(const char* name,
                                int sample,
                                int exclusive_max) {
  base::AutoLock lock(*g_metrics_lock);
  if (g_metrics) {
    g_metrics->SendLinearToUMA(name, sample, exclusive_max);
  }
}

void RecordCustomCountsHistogram(
    const char* name, int sample, int min, int exclusive_max, size_t buckets) {
  base::AutoLock lock(*g_metrics_lock);
  if (g_metrics) {
    g_metrics->SendToUMA(name, sample, min, exclusive_max, buckets);
  }
}

}  // namespace

ChromeML::ChromeML(raw_ref<MetricsLibraryInterface> metrics,
                   base::PassKey<ChromeML>,
                   const ChromeMLAPI* api)
    : api_(api) {
  CHECK(api_);
  base::AutoLock lock(*g_metrics_lock);
  CHECK(!g_metrics || g_metrics == &metrics.get());
  g_chrome_ml_count++;
  g_metrics = &metrics.get();
}

ChromeML::~ChromeML() {
  base::AutoLock lock(*g_metrics_lock);
  CHECK(g_metrics);
  g_chrome_ml_count--;
  if (g_chrome_ml_count == 0) {
    g_metrics = nullptr;
  }
}

// static
ChromeML* ChromeML::Get(raw_ref<MetricsLibraryInterface> metrics,
                        raw_ref<odml::OdmlShimLoader> shim_loader) {
  static base::NoDestructor<std::unique_ptr<ChromeML>> chrome_ml{
      Create(metrics, shim_loader)};

  // Init the global chrome_ml pointer.
  [[maybe_unused]] static bool init = []() {
    g_chrome_ml.store(chrome_ml->get());
    return true;
  }();

  return chrome_ml->get();
}

ChromeML* ChromeML::Get() {
  return g_chrome_ml;
}

// static
DISABLE_CFI_DLSYM
std::unique_ptr<ChromeML> ChromeML::Create(
    raw_ref<MetricsLibraryInterface> metrics,
    raw_ref<odml::OdmlShimLoader> shim_loader) {
  auto get_api = shim_loader->Get<ChromeMLAPIGetter>("GetChromeMLAPI");
  if (!get_api) {
    LOG(ERROR) << "Unable to resolve GetChromeMLAPI() symbol.";
    return {};
  }

  const ChromeMLAPI* api = get_api();
  if (!api) {
    return {};
  }

  auto get_dawn = shim_loader->Get<DawnNativeProcsGetter>("GetDawnNativeProcs");
  if (!get_dawn) {
    LOG(ERROR) << "Unable to resolve GetChromeMLAPI() symbol.";
    return {};
  }

  const DawnProcTable* dawn_proc_table = get_dawn();
  if (!dawn_proc_table) {
    LOG(ERROR) << "Unable to get_dawn.";
    return {};
  }

  api->InitDawnProcs(*dawn_proc_table);
  if (api->SetFatalErrorFn) {
    api->SetFatalErrorFn(&FatalGpuErrorFn);
  }
  if (api->SetMetricsFns) {
    const ChromeMLMetricsFns metrics_fns{
        .RecordExactLinearHistogram = &RecordExactLinearHistogram,
        .RecordCustomCountsHistogram = &RecordCustomCountsHistogram,
    };
    api->SetMetricsFns(&metrics_fns);
  }
  if (api->SetFatalErrorNonGpuFn) {
    api->SetFatalErrorNonGpuFn(&FatalErrorFn);
  }
  return std::make_unique<ChromeML>(metrics, base::PassKey<ChromeML>(), api);
}

DISABLE_CFI_DLSYM
bool ChromeML::IsGpuBlocked() const {
  // We wouldn't block GPU on ChromeOS devices.
  return false;
}

}  // namespace ml
