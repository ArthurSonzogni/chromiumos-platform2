// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/metric_utils.h"

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/files/file_util.h>
#include <brillo/process/process.h>

#include "crash-reporter/crash_collection_status.h"
#include "crash-reporter/crash_collector_names.h"
#include "crash-reporter/crash_sending_mode.h"
#include "crash-reporter/util.h"

namespace {
#if !USE_KVM_GUEST
constexpr char kMetricsClientPath[] = "/usr/bin/metrics_client";
constexpr char kCrashReportingProjectName[] = "CrashReporting";

util::BrilloProcessFactory* GetDefaultFactory() {
  static util::BrilloProcessFactory default_factory;
  return &default_factory;
}

util::BrilloProcessFactory* g_factory = GetDefaultFactory();
#endif  // !USE_KVM_GUEST
// Record a metric by invoking metrics_client.
// crash-reporter and crash-sender record metrics by invoking metric_client
// instead of directly calling structured metrics library functions. This is
// to isolate the crash reporting system from any issues with the structured
// metrics system. Since these metrics are about the only way we have of
// checking that crash-reporter and crash-sender aren't crashing, it's
// especially important to avoid having the main process crash if there is a
// bug in the structured metrics implementation.
void InvokeMetricsClient(const std::string& event_name,
                         const std::vector<std::string>& event_args) {
// Metrics are not recorded inside a VM since there is nowhere to record them.
// TODO(b/343493432): Record metrics inside VMs
#if !USE_KVM_GUEST
  std::unique_ptr<brillo::Process> process = g_factory->CreateProcess();
  process->AddArg(kMetricsClientPath);
  process->AddArg("--structured");
  process->AddArg(kCrashReportingProjectName);
  process->AddArg(event_name);

  for (const std::string& arg : event_args) {
    process->AddArg(arg);
  }

  base::FilePath output;
  if (base::CreateTemporaryFile(&output)) {
    process->RedirectOutput(output);
  } else {
    LOG(WARNING) << "Failed to create temp file for metrics_client output";
    output.clear();
    // This was just for the sake of error logging, so keep going.
  }

  int result = process->Run();
  if (result != 0) {
    std::string output_text;
    // Pretty arbitrary, should be more than large enough to capture any
    // error messages but small enough to not risk memory exhaustion.
    constexpr int kMaxOutputSize = 64 * 1024;
    if (output.empty()) {
      output_text = "<could not create temp output file>";
    } else {
      // Note that ReadFileToStringWithMaxSize can return false on partial
      // reads. If we got a partial read, use that for the log message;
      // only use "<could not read temp output file>" if we got nothing.
      bool success = base::ReadFileToStringWithMaxSize(output, &output_text,
                                                       kMaxOutputSize);
      if (!success && output_text.empty()) {
        output_text = "<could not read temp output file>";
      }
    }
    std::string complete_args = base::StrCat(
        {kMetricsClientPath, " --structured ", kCrashReportingProjectName, " ",
         event_name, " ", base::JoinString(event_args, " ")});
    LOG(ERROR) << "Failed to invoke " << complete_args << ": exit code "
               << result << " with output: " << output_text;
  }

  if (!output.empty()) {
    brillo::DeleteFile(output);
  }
#endif  // !USE_KVM_GUEST
}
}  // namespace

CrashReporterStatusRecorder RecordCrashReporterStart(
    CrashReporterCollector collector, CrashSendingMode crash_sending_mode) {
  InvokeMetricsClient(
      "CrashReporterStart",
      {
          "--Collector",
          base::NumberToString(static_cast<int>(collector)),
          "--CrashSendingMode",
          base::NumberToString(static_cast<int>(crash_sending_mode)),
      });

  return CrashReporterStatusRecorder(collector, crash_sending_mode);
}

CrashReporterStatusRecorder::CrashReporterStatusRecorder(
    CrashReporterCollector collector, CrashSendingMode crash_sending_mode)
    : collector_(collector), crash_sending_mode_(crash_sending_mode) {}

CrashReporterStatusRecorder::~CrashReporterStatusRecorder() {
  InvokeMetricsClient(
      "CrashReporterStatus",
      {
          "--Status",
          base::NumberToString(static_cast<int>(status_)),
          "--Collector",
          base::NumberToString(static_cast<int>(collector_)),
          "--CrashSendingMode",
          base::NumberToString(static_cast<int>(crash_sending_mode_)),
      });
}

void OverrideBrilloProcessFactoryForTesting(
    util::BrilloProcessFactory* factory) {
#if !USE_KVM_GUEST
  if (factory == nullptr) {
    g_factory = GetDefaultFactory();
  } else {
    g_factory = factory;
  }
#endif  // !USE_KVM_GUEST
}
