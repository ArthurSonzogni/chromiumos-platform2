// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/service_failure_collector.h"

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>

#include "crash-reporter/util.h"

namespace {
const char kExecName[] = "service-failure";
const char kSignatureKey[] = "sig";
const char kFailureReportPath[] = "/run/anomaly-collector/service-fail";
}  // namespace

using base::FilePath;
using base::StringPrintf;

ServiceFailureCollector::ServiceFailureCollector()
    : failure_report_path_(kFailureReportPath) {}

ServiceFailureCollector::~ServiceFailureCollector() {}

bool ServiceFailureCollector::LoadServiceFailure(std::string* signature) {
  FilePath failure_report_path(failure_report_path_);
  if (!base::ReadFileToString(failure_report_path, signature)) {
    LOG(ERROR) << "Could not open " << failure_report_path_;
    return false;
  }
  // The report is a single line with the signature. Chop off the first newline
  // character and anything that might follow it.
  std::string::size_type end_position = signature->find('\n');
  if (end_position != std::string::npos) {
    signature->resize(end_position);
  }

  return !signature->empty();
}

bool ServiceFailureCollector::Collect() {
  std::string reason = "normal collection";
  bool feedback = true;
  if (util::IsDeveloperImage()) {
    reason = "always collect from developer builds";
    feedback = true;
  } else if (!is_feedback_allowed_function_()) {
    reason = "no user consent";
    feedback = false;
  }

  LOG(INFO) << "Processing service failure: " << reason;

  if (!feedback) {
    return true;
  }

  std::string failure_signature;
  if (!LoadServiceFailure(&failure_signature)) {
    return true;
  }

  FilePath crash_directory;
  if (!GetCreatedCrashDirectoryByEuid(kRootUid, &crash_directory, nullptr)) {
    return true;
  }

  std::string dump_basename = FormatDumpBasename(kExecName, time(nullptr), 0);
  FilePath log_path = GetCrashPath(crash_directory, dump_basename, "log");
  FilePath meta_path = GetCrashPath(crash_directory, dump_basename, "meta");

  AddCrashMetaData(kSignatureKey, failure_signature);

  bool result = GetLogContents(log_config_path_, kExecName, log_path);
  if (result) {
    WriteCrashMetaData(meta_path, kExecName, log_path.value());
  }

  return true;
}
