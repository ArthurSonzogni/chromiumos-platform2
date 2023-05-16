// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/events/crash_events.h"

#include <string>
#include <utility>

#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

namespace diagnostics {
namespace mojom = ash::cros_healthd::mojom;

namespace {
// Parses a single log entry and returns the result.
mojom::CrashEventInfoPtr ParseUploadsLogEntry(const std::string& line,
                                              bool is_uploaded,
                                              base::Time creation_time,
                                              uint64_t offset) {
  // The SplitString call guarantees that line can't be empty.
  DCHECK(!line.empty());

  const auto json = base::JSONReader::Read(line);
  if (!json.has_value() || !json->is_dict()) {
    LOG(ERROR) << "Invalid JSON in crash uploads log: " << line;
    return nullptr;
  }

  // Extract relevant fields.
  auto result = mojom::CrashEventInfo::New();
  const auto& json_dict = json->GetDict();

  // crash_type
  result->crash_type = mojom::CrashEventInfo::CrashType::kUnknown;
  if (const auto* crash_type = json_dict.FindString("fatal_crash_type");
      crash_type != nullptr) {
    if (*crash_type == "kernel") {
      result->crash_type = mojom::CrashEventInfo::CrashType::kKernel;
    } else if (*crash_type == "ec") {
      result->crash_type =
          mojom::CrashEventInfo::CrashType::kEmbeddedController;
    }
  }

  // crash_report_id
  if (is_uploaded) {
    if (const auto* crash_report_id = json_dict.FindString("upload_id");
        crash_report_id != nullptr) {
      result->upload_info =
          mojom::CrashUploadInfo::New(*crash_report_id, creation_time, offset);
    } else {
      LOG(ERROR) << "Crash report ID is not found while the crash has been "
                    "uploaded: "
                 << line;
      return nullptr;
    }
  }

  // local_id
  if (const auto* local_id = json_dict.FindString("path_hash");
      local_id != nullptr) {
    result->local_id = *local_id;
  } else {
    LOG(ERROR) << "Local ID not found: " << line;
    return nullptr;
  }

  // capture_time
  const auto* capture_time_string = json_dict.FindString("capture_time");
  if (capture_time_string == nullptr) {
    LOG(ERROR) << "Capture time not found: " << line;
    return nullptr;
  }
  double capture_time_double;
  if (!base::StringToDouble(*capture_time_string, &capture_time_double)) {
    LOG(ERROR) << "Invalid capture time: " << line;
    return nullptr;
  }
  result->capture_time = base::Time::FromDoubleT(capture_time_double);

  return result;
}  // namespace =::mojom
}  // namespace

std::vector<mojom::CrashEventInfoPtr> ParseUploadsLog(base::StringPiece log,
                                                      bool is_uploaded,
                                                      base::Time creation_time,
                                                      uint64_t init_offset,
                                                      uint64_t* parsed_bytes) {
  if (parsed_bytes) {
    *parsed_bytes = log.size();
  }
  std::vector<mojom::CrashEventInfoPtr> result;
  // Using whitespace (instead of line breakers) as the delimiter here is a
  // bit odd, but this is what `TextLogUploadList::SplitIntoLines` does.
  const auto log_lines =
      base::SplitString(log, base::kWhitespaceASCII, base::KEEP_WHITESPACE,
                        base::SPLIT_WANT_NONEMPTY);
  for (size_t i = 0; i < log_lines.size(); ++i) {
    const auto& line = log_lines[i];
    // each line is a log entry, from which we can extract crash info.
    auto log_entry = ParseUploadsLogEntry(line, is_uploaded, creation_time,
                                          init_offset + result.size());
    if (log_entry.is_null()) {
      // The last log line requires some special processing for parsed_bytes.
      if (i == log_lines.size() - 1 && parsed_bytes &&
          !base::IsAsciiWhitespace(log.back())) {
        CHECK_GE(log.size(), line.size());
        *parsed_bytes = log.size() - line.size();
      }
      continue;
    }
    result.push_back(std::move(log_entry));
  }

  return result;
}
}  // namespace diagnostics
