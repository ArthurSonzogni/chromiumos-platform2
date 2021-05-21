// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/arc_java_collector.h"

#include <ctime>
#include <utility>

#include <base/files/file.h>
#include <base/logging.h>
#include <base/time/time.h>

#include "crash-reporter/arc_util.h"
#include "crash-reporter/util.h"

using base::File;
using base::FilePath;

namespace {

constexpr char kArcJavaCollectorName[] = "ARC_java";
constexpr size_t kBufferSize = 4096;

bool ReadCrashLogFromStdin(std::stringstream* stream);

}  // namespace

ArcJavaCollector::ArcJavaCollector()
    : CrashCollector(kArcJavaCollectorName,
                     kAlwaysUseUserCrashDirectory,
                     kNormalCrashSendMode,
                     kArcJavaCollectorName) {}

bool ArcJavaCollector::HandleCrash(
    const std::string& crash_type,
    const arc_util::BuildProperty& build_property,
    base::TimeDelta uptime) {
  std::ostringstream message;
  message << "Received " << crash_type << " notification";

  std::stringstream stream;
  if (!ReadCrashLogFromStdin(&stream)) {
    PLOG(ERROR) << "Failed to read crash log";
    return false;
  }

  CrashLogHeaderMap map;
  std::string exception_info, log;
  if (!arc_util::ParseCrashLog(crash_type, &stream, &map, &exception_info,
                               &log)) {
    LOG(ERROR) << "Failed to parse crash log";
    return false;
  }

  const auto exec = arc_util::GetCrashLogHeader(map, arc_util::kProcessKey);
  message << " for " << exec;
  LogCrash(message.str(), "handling");

  bool out_of_capacity = false;
  if (!CreateReportForJavaCrash(crash_type, build_property, map, exception_info,
                                log, uptime, &out_of_capacity)) {
    if (!out_of_capacity) {
      EnqueueCollectionErrorLog(kErrorSystemIssue, exec);
    }
    return false;
  }

  return true;
}

std::string ArcJavaCollector::GetProductVersion() const {
  std::string version;
  return arc_util::GetChromeVersion(&version) ? version : kUnknownValue;
}

void ArcJavaCollector::AddArcMetaData(const std::string& process,
                                      const std::string& crash_type,
                                      base::TimeDelta uptime) {
  AddCrashMetaUploadData(arc_util::kProductField, arc_util::kArcProduct);
  AddCrashMetaUploadData(arc_util::kProcessField, process);
  AddCrashMetaUploadData(arc_util::kCrashTypeField, crash_type);
  AddCrashMetaUploadData(arc_util::kChromeOsVersionField, GetOsVersion());

#if USE_ARCPP
  if (uptime.is_zero()) {
    SetUpDBus();
    if (!arc_util::GetArcContainerUptime(session_manager_proxy_.get(),
                                         &uptime)) {
      uptime = base::TimeDelta();
    }
  }
#endif  // USE_ARCPP
  if (!uptime.is_zero()) {
    AddCrashMetaUploadData(arc_util::kUptimeField,
                           arc_util::FormatDuration(uptime));
  }

  if (arc_util::IsSilentReport(crash_type))
    AddCrashMetaData(arc_util::kSilentKey, "true");
}

bool ArcJavaCollector::CreateReportForJavaCrash(
    const std::string& crash_type,
    const arc_util::BuildProperty& build_property,
    const CrashLogHeaderMap& map,
    const std::string& exception_info,
    const std::string& log,
    base::TimeDelta uptime,
    bool* out_of_capacity) {
  FilePath crash_dir;
  if (!GetCreatedCrashDirectoryByEuid(geteuid(), &crash_dir, out_of_capacity)) {
    LOG(ERROR) << "Failed to create or find crash directory";
    return false;
  }

  const auto process = arc_util::GetCrashLogHeader(map, arc_util::kProcessKey);
  pid_t dt = arc_util::CreateRandomPID();
  const auto basename = FormatDumpBasename(process, std::time(nullptr), dt);
  const FilePath log_path = GetCrashPath(crash_dir, basename, "log");

  const int size = static_cast<int>(log.size());
  if (WriteNewFile(log_path, log.c_str(), size) != size) {
    PLOG(ERROR) << "Failed to write log";
    return false;
  }

  AddArcMetaData(process, crash_type, uptime);
  for (auto metadata : arc_util::ListMetadataForBuildProperty(build_property)) {
    AddCrashMetaUploadData(metadata.first, metadata.second);
  }

  for (const auto& mapping : arc_util::kHeaderToFieldMapping) {
    if (map.count(mapping.first)) {
      AddCrashMetaUploadData(mapping.second,
                             arc_util::GetCrashLogHeader(map, mapping.first));
    }
  }

  if (exception_info.empty()) {
    if (const char* const tag = arc_util::GetSubjectTag(crash_type)) {
      std::ostringstream out;
      out << '[' << tag << ']';
      const auto it = map.find(arc_util::kSubjectKey);
      if (it != map.end())
        out << ' ' << it->second;

      AddCrashMetaData(arc_util::kSignatureField, out.str());
    } else {
      LOG(ERROR) << "Invalid crash type: " << crash_type;
      return false;
    }
  } else {
    const FilePath info_path = GetCrashPath(crash_dir, basename, "info");
    const int size = static_cast<int>(exception_info.size());

    if (WriteNewFile(info_path, exception_info.c_str(), size) != size) {
      PLOG(ERROR) << "Failed to write exception info";
      return false;
    }

    AddCrashMetaUploadText(arc_util::kExceptionInfoField,
                           info_path.BaseName().value());
  }

  const FilePath meta_path = GetCrashPath(crash_dir, basename, "meta");
  FinishCrash(meta_path, process, log_path.BaseName().value());
  return true;
}

namespace {

bool ReadCrashLogFromStdin(std::stringstream* stream) {
  File src(STDIN_FILENO);
  char buffer[kBufferSize];

  while (true) {
    const int count = src.ReadAtCurrentPosNoBestEffort(buffer, kBufferSize);
    if (count < 0)
      return false;

    if (count == 0)
      return stream->tellp() > 0;  // Crash log should not be empty.

    stream->write(buffer, count);
  }
}

}  // namespace
