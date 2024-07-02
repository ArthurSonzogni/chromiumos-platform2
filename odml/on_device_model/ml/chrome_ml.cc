// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/on_device_model/ml/chrome_ml.h"

#include <base/base_paths.h>
#include <base/check.h>
#include <base/compiler_specific.h>
#include <base/debug/crash_logging.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/metrics/histogram_functions.h>
#include <base/native_library.h>
#include <base/no_destructor.h>
#include <base/path_service.h>
#include <base/process/process.h>
#include <build/build_config.h>

#include <string_view>

namespace ml {

namespace {

// Signature of the GetDawnNativeProcs() function which the shared library
// exports.
using DawnNativeProcsGetter = const DawnProcTable* (*)();

constexpr std::string_view kChromeMLLibraryName = "odml_shim";

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class GpuErrorReason {
  kOther = 0,
  kDxgiErrorDeviceHung = 1,
  kDxgiErrorDeviceRemoved = 2,
  kMaxValue = kDxgiErrorDeviceRemoved,
};

void FatalGpuErrorFn(const char* msg) {
  SCOPED_CRASH_KEY_STRING1024("ChromeML(GPU)", "error_msg", msg);
  std::string msg_str(msg);
  GpuErrorReason error_reason = GpuErrorReason::kOther;
  if (msg_str.find("DXGI_ERROR_DEVICE_HUNG") != std::string::npos) {
    error_reason = GpuErrorReason::kDxgiErrorDeviceHung;
  } else if (msg_str.find("DXGI_ERROR_DEVICE_REMOVED") != std::string::npos) {
    error_reason = GpuErrorReason::kDxgiErrorDeviceRemoved;
  }
  base::UmaHistogramEnumeration("OnDeviceModel.GpuErrorReason", error_reason);
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
  base::UmaHistogramExactLinear(name, sample, exclusive_max);
}

void RecordCustomCountsHistogram(
    const char* name, int sample, int min, int exclusive_max, size_t buckets) {
  base::UmaHistogramCustomCounts(name, sample, min, exclusive_max, buckets);
}

}  // namespace

ChromeML::ChromeML(base::PassKey<ChromeML>,
                   base::ScopedNativeLibrary library,
                   const ChromeMLAPI* api)
    : library_(std::move(library)), api_(api) {
  CHECK(api_);
}

ChromeML::~ChromeML() = default;

// static
ChromeML* ChromeML::Get(const std::optional<std::string>& library_name) {
  static base::NoDestructor<std::unique_ptr<ChromeML>> chrome_ml{
      Create(library_name)};
  return chrome_ml->get();
}

// static
DISABLE_CFI_DLSYM
std::unique_ptr<ChromeML> ChromeML::Create(
    const std::optional<std::string>& library_name) {
  base::NativeLibraryLoadError error;
  base::NativeLibrary library = base::LoadNativeLibrary(
      base::FilePath(base::GetNativeLibraryName(
          library_name.value_or(std::string(kChromeMLLibraryName)))),
      &error);
  if (!library) {
    LOG(ERROR) << "Error loading native library: " << error.ToString();
    return {};
  }

  base::ScopedNativeLibrary scoped_library(library);
  auto get_api = reinterpret_cast<ChromeMLAPIGetter>(
      scoped_library.GetFunctionPointer("GetChromeMLAPI"));
  if (!get_api) {
    LOG(ERROR) << "Unable to resolve GetChromeMLAPI() symbol.";
    return {};
  }

  const ChromeMLAPI* api = get_api();
  if (!api) {
    return {};
  }

  auto get_dawn = reinterpret_cast<DawnNativeProcsGetter>(
      scoped_library.GetFunctionPointer("GetDawnNativeProcs"));

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
  return std::make_unique<ChromeML>(base::PassKey<ChromeML>(),
                                    std::move(scoped_library), api);
}

DISABLE_CFI_DLSYM
bool ChromeML::IsGpuBlocked() const {
  // We wouldn't block GPU on ChromeOS devices.
  return false;
}

}  // namespace ml
