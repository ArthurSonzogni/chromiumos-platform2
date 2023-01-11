// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/boot_performance_fetcher.h"

#include <cctype>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/time/time.h>
#include <metrics/bootstat.h>
#include <re2/re2.h>

#include "diagnostics/base/file_utils.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"

namespace diagnostics {
namespace {

namespace mojo_ipc = ::ash::cros_healthd::mojom;

std::optional<mojo_ipc::ProbeErrorPtr> ParseBootFirmwareTime(
    double* firmware_time) {
  const auto& data_path = GetRootedPath(path::kBiosTimes);
  std::string content;
  if (!ReadAndTrimString(data_path, &content)) {
    return CreateAndLogProbeError(mojo_ipc::ErrorType::kFileReadError,
                                  "Failed to read file: " + data_path.value());
  }

  std::vector<std::string> lines = base::SplitString(
      content, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  std::string value;
  const char regex[] = R"(Total Time: (.*))";
  // The target line is super close to the end of the log.
  // Example of target line:
  // Total Time: 14,630,633
  for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
    if (RE2::FullMatch(*it, regex, &value)) {
      break;
    }
  }

  if (value.empty()) {
    return CreateAndLogProbeError(mojo_ipc::ErrorType::kParseError,
                                  "Failed to parse file: " + data_path.value());
  }

  // value is "14,630,633", we need to remove the comma.
  value.erase(remove(value.begin(), value.end(), ','), value.end());
  if (!base::StringToDouble(value, firmware_time)) {
    return CreateAndLogProbeError(mojo_ipc::ErrorType::kParseError,
                                  "Failed to parse total time value: " + value);
  }
  *firmware_time = *firmware_time / base::Time::kMicrosecondsPerSecond;

  return std::nullopt;
}

std::optional<mojo_ipc::ProbeErrorPtr> ParseBootKernelTime(
    double* kernel_time) {
  auto events = bootstat::BootStat(GetRootedPath("/"))
                    .GetEventTimings("login-prompt-visible");
  if (!events || events->empty()) {
    return CreateAndLogProbeError(mojo_ipc::ErrorType::kFileReadError,
                                  "Failed to get login-prompt stats");
  }

  // There may be multiple events; we only care about the first occurrence.
  *kernel_time = (*events)[0].uptime.InSecondsF();
  return std::nullopt;
}

std::optional<mojo_ipc::ProbeErrorPtr> ParseProcUptime(double* proc_uptime) {
  const auto& data_path = GetRootedPath(path::kProcUptime);
  std::string content;
  if (!ReadAndTrimString(data_path, &content)) {
    return CreateAndLogProbeError(mojo_ipc::ErrorType::kFileReadError,
                                  "Failed to read file: " + data_path.value());
  }

  // There is only one line in the content, here is an example:
  // 68061.02 520871.89
  // The first record is the total seconds after kernel is up.
  auto value = content.substr(0, content.find_first_of(" "));
  if (!base::StringToDouble(value, proc_uptime)) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kParseError,
        "Failed to parse /proc/uptime value: " + value);
  }

  return std::nullopt;
}

std::optional<mojo_ipc::ProbeErrorPtr> PopulateBootUpInfo(
    mojo_ipc::BootPerformanceInfo* info) {
  // Boot up stages
  //                              |<-             proc_uptime     ->
  //          |<- firmware_time ->|<-  kernel_time  ->|
  //  |-------|-------------------|-------------------|------------> Now
  // off   power on         jump to kernel       login screen
  //
  // There is some deviation when calculating, but it should be minor.
  // See go/chromeos-boottime for more details.
  info->boot_up_seconds = 0.0;
  info->boot_up_timestamp = 0;

  double firmware_time;
  auto error = ParseBootFirmwareTime(&firmware_time);
  if (error.has_value()) {
    return error;
  }
  info->boot_up_seconds += firmware_time;

  double kernel_time;
  error = ParseBootKernelTime(&kernel_time);
  if (error.has_value()) {
    return error;
  }
  info->boot_up_seconds += kernel_time;

  double proc_uptime;
  error = ParseProcUptime(&proc_uptime);
  if (error.has_value()) {
    return error;
  }
  // Calculate the timestamp when power on.
  info->boot_up_timestamp =
      base::Time::Now().ToDoubleT() - proc_uptime - firmware_time;

  return std::nullopt;
}

bool ParsePreviousPowerdLog(double* shutdown_start_timestamp,
                            std::string* shutdown_reason) {
  const auto& data_path = GetRootedPath(path::kPreviousPowerdLog);
  std::string content;
  if (!ReadAndTrimString(data_path, &content)) {
    return false;
  }

  std::vector<std::string> lines = base::SplitString(
      content, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  const char shutdown_regex[] =
      R"((.*)Z INFO powerd:.*Shutting down, reason: (.*))";
  const char restart_regex[] =
      R"((.*)Z INFO powerd:.*Restarting, reason: (.*))";
  const int max_parsed_line = 300;

  std::string time_raw;
  int parsed_line_cnt = 0;
  // The target line is super close to the end of the log.
  for (auto it = lines.rbegin();
       it != lines.rend() && parsed_line_cnt < max_parsed_line;
       ++it, ++parsed_line_cnt) {
    if (RE2::FullMatch(*it, shutdown_regex, &time_raw, shutdown_reason) ||
        RE2::FullMatch(*it, restart_regex, &time_raw, shutdown_reason)) {
      base::Time time;
      if (base::Time::FromUTCString(time_raw.c_str(), &time)) {
        *shutdown_start_timestamp = time.ToDoubleT();
      }
      break;
    }
  }

  return !shutdown_reason->empty();
}

bool GetShutdownEndTimestamp(double* shutdown_end_timestamp) {
  const auto& data_path = GetRootedPath(path::kShutdownMetrics);
  base::File::Info file_info;
  if (!GetFileInfo(data_path, &file_info)) {
    return false;
  }

  *shutdown_end_timestamp = file_info.last_modified.ToDoubleT();

  return true;
}

void PopulateShutdownInfo(mojo_ipc::BootPerformanceInfo* info) {
  // Shutdown stages
  //
  //           |<-     shutdown seconds      ->|
  // running --|-------------------------------|-------------------|------> off
  // powerd receives request          create metrics log   unmount partition
  double shutdown_start_timestamp;
  double shutdown_end_timestamp;
  std::string shutdown_reason;

  if (!ParsePreviousPowerdLog(&shutdown_start_timestamp, &shutdown_reason) ||
      !GetShutdownEndTimestamp(&shutdown_end_timestamp) ||
      shutdown_end_timestamp < shutdown_start_timestamp) {
    info->shutdown_reason = "N/A";
    info->shutdown_timestamp = 0.0;
    info->shutdown_seconds = 0.0;
    return;
  }

  info->shutdown_reason = shutdown_reason;
  info->shutdown_timestamp = shutdown_end_timestamp;
  info->shutdown_seconds = shutdown_end_timestamp - shutdown_start_timestamp;
}

}  // namespace

mojo_ipc::BootPerformanceResultPtr FetchBootPerformanceInfo() {
  mojo_ipc::BootPerformanceInfo info;
  auto error = PopulateBootUpInfo(&info);
  if (error.has_value()) {
    return mojo_ipc::BootPerformanceResult::NewError(std::move(error.value()));
  }

  // There might be no shutdown info, so we don't check if there is any error.
  PopulateShutdownInfo(&info);

  return mojo_ipc::BootPerformanceResult::NewBootPerformanceInfo(info.Clone());
}

}  // namespace diagnostics
