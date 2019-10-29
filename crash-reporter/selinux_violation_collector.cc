// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/selinux_violation_collector.h"

#include <map>

#include <base/files/file_util.h>
#include <base/logging.h>

#include "crash-reporter/util.h"

namespace {
constexpr char kExecName[] = "selinux-violation";
constexpr char kSignatureKey[] = "sig";
}  // namespace

using base::FilePath;
using base::StringPrintf;

SELinuxViolationCollector::SELinuxViolationCollector()
    : CrashCollector("selinux"), violation_report_path_("/dev/stdin") {}

SELinuxViolationCollector::~SELinuxViolationCollector() {}

bool SELinuxViolationCollector::LoadSELinuxViolation(
    std::string* content,
    std::string* signature,
    std::map<std::string, std::string>* extra_metadata) {
  std::string violation_report;
  if (!base::ReadFileToString(violation_report_path_, &violation_report)) {
    PLOG(ERROR) << "Could not open " << violation_report_path_.value();
    return false;
  }

  // Report format
  // First line:  signature
  // Second line: parsed metadata key\x01value\x02key\x01value\x02
  // Third+ line: content

  std::string::size_type signature_end_position = violation_report.find('\n');
  *signature = violation_report.substr(0, signature_end_position);

  violation_report = violation_report.substr(signature_end_position + 1);
  std::string::size_type metadata_end_position = violation_report.find('\n');
  *content = violation_report.substr(metadata_end_position + 1);

  std::string metadata_string =
      violation_report.substr(0, metadata_end_position);
  while (!metadata_string.empty()) {
    std::string::size_type key_end_position = metadata_string.find('\x01');
    std::string::size_type value_end_position = metadata_string.find('\x02');
    std::string key = metadata_string.substr(0, key_end_position);
    std::string value = metadata_string.substr(
        key_end_position + 1, value_end_position - key_end_position - 1);
    extra_metadata->emplace(key, value);
    metadata_string = metadata_string.substr(value_end_position + 1);
  }

  return !signature->empty();
}

bool SELinuxViolationCollector::Collect() {
  std::string reason = "normal collection";
  bool feedback = true;
  if (util::IsDeveloperImage() || developer_image_for_testing_) {
    feedback = true;
    reason = "always collect from developer builds";
  } else if (!is_feedback_allowed_function_()) {
    reason = "no user consent";
    feedback = false;
  }
  LOG(INFO) << "Processing selinux violation: " << reason;

  if (!feedback)
    return true;

  std::string violation_signature;
  std::string content;
  std::map<std::string, std::string> extra_metadata;
  if (!LoadSELinuxViolation(&content, &violation_signature, &extra_metadata))
    return true;

  FilePath crash_directory;
  if (!GetCreatedCrashDirectoryByEuid(kRootUid, &crash_directory, nullptr))
    return true;

  std::string dump_basename = FormatDumpBasename(kExecName, time(nullptr), 0);
  FilePath meta_path = GetCrashPath(crash_directory, dump_basename, "meta");
  FilePath log_path = GetCrashPath(crash_directory, dump_basename, "log");

  if (WriteNewFile(log_path, content.data(), content.length()) !=
      static_cast<int>(content.length())) {
    PLOG(WARNING) << "Failed to write audit message to " << log_path.value();
    return true;
  }

  AddCrashMetaUploadData("weight",
                         StringPrintf("%d", util::GetSelinuxWeight()));

  AddCrashMetaData(kSignatureKey, violation_signature);

  for (const auto& metadata : extra_metadata)
    AddCrashMetaUploadData(metadata.first, metadata.second);

  FinishCrash(meta_path, kExecName, log_path.BaseName().value());

  return true;
}
