// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/crash_reporter_failure_collector.h"

#include <memory>
#include <string>

#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <metrics/metrics_library.h>

#include "crash-reporter/constants.h"

using base::FilePath;

namespace {
const char kExecName[] = "crash_reporter_failure";
}  // namespace

CrashReporterFailureCollector::CrashReporterFailureCollector(
    const scoped_refptr<
        base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>&
        metrics_lib)
    : CrashCollector("crash-reporter-failure-collector", metrics_lib) {}

CrashReporterFailureCollector::~CrashReporterFailureCollector() {}

bool CrashReporterFailureCollector::Collect() {
  LOG(INFO) << "Detected crash_reporter failure";

  FilePath crash_directory;
  if (!GetCreatedCrashDirectoryByEuid(constants::kRootUid, &crash_directory,
                                      nullptr)) {
    return false;
  }

  std::string dump_basename = FormatDumpBasename(kExecName, time(nullptr), 0);
  FilePath log_path = GetCrashPath(crash_directory, dump_basename, "log");
  FilePath meta_path = GetCrashPath(crash_directory, dump_basename, "meta");

  bool result = GetLogContents(log_config_path_, kExecName, log_path);
  if (result) {
    FinishCrash(meta_path, kExecName, log_path.BaseName().value());
  }
  return true;
}

CrashCollector::ComputedCrashSeverity
CrashReporterFailureCollector::ComputeSeverity(const std::string& exec_name) {
  return ComputedCrashSeverity{
      .crash_severity = CrashSeverity::kInfo,
      .product_group = Product::kPlatform,
  };
}

// static
CollectorInfo CrashReporterFailureCollector::GetHandlerInfo(
    bool crash_reporter_crashed,
    const scoped_refptr<
        base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>&
        metrics_lib) {
  auto crash_reporter_failure_collector =
      std::make_shared<CrashReporterFailureCollector>(metrics_lib);
  return {.collector = crash_reporter_failure_collector,
          .handlers = {{
              .should_handle = crash_reporter_crashed,
              .cb = base::BindRepeating(&CrashReporterFailureCollector::Collect,
                                        crash_reporter_failure_collector),
          }}};
}
