// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cctype>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <re2/re2.h>

#include <chromeos/dbus/service_constants.h>
#include "diagnostics/cros_healthd/fetchers/boot_performance_fetcher.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/cros_healthd/utils/file_utils.h"
#include "diagnostics/cros_healthd/utils/procfs_utils.h"

#include <utility>

namespace diagnostics {

constexpr char kRelativeBiosTimesPath[] = "var/log/bios_times.txt";
constexpr char kRelativeUptimeLoginPath[] = "tmp/uptime-login-prompt-visible";
constexpr char kRelativeShutdownMetricsPath[] = "var/log/metrics";
constexpr char kRelativePreviousPowerdLogPath[] =
    "var/log/power_manager/powerd.PREVIOUS";

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

}  // namespace

mojo_ipc::BootPerformanceResultPtr
BootPerformanceFetcher::FetchBootPerformanceInfo() {
  mojo_ipc::BootPerformanceInfo info;

  auto error = PopulateBootUpInfo(&info);
  if (error.has_value()) {
    return mojo_ipc::BootPerformanceResult::NewError(std::move(error.value()));
  }

  // There might be no shutdown info, so we don't check if there is any error.
  PopulateShutdownInfo(&info);

  return mojo_ipc::BootPerformanceResult::NewBootPerformanceInfo(info.Clone());
}

base::Optional<mojo_ipc::ProbeErrorPtr>
BootPerformanceFetcher::PopulateBootUpInfo(
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
      context_->time().ToDoubleT() - proc_uptime - firmware_time;

  return base::nullopt;
}

base::Optional<mojo_ipc::ProbeErrorPtr>
BootPerformanceFetcher::ParseBootFirmwareTime(double* firmware_time) {
  const auto& data_path = context_->root_dir().Append(kRelativeBiosTimesPath);
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

  return base::nullopt;
}

base::Optional<mojo_ipc::ProbeErrorPtr>
BootPerformanceFetcher::ParseBootKernelTime(double* kernel_time) {
  const auto& data_path = context_->root_dir().Append(kRelativeUptimeLoginPath);
  std::string content;
  if (!ReadAndTrimString(data_path, &content)) {
    return CreateAndLogProbeError(mojo_ipc::ErrorType::kFileReadError,
                                  "Failed to read file: " + data_path.value());
  }

  // There might by multiple lines in the content, here is an example:
  // 6.535802230\n37.258371903\n129.271920462
  // We only care about the first occurrence.
  auto value = content.substr(0, content.find_first_of("\n"));
  if (!base::StringToDouble(value, kernel_time)) {
    return CreateAndLogProbeError(mojo_ipc::ErrorType::kParseError,
                                  "Failed to parse uptime log value: " + value);
  }

  return base::nullopt;
}

base::Optional<mojo_ipc::ProbeErrorPtr> BootPerformanceFetcher::ParseProcUptime(
    double* proc_uptime) {
  const auto& data_path = GetProcUptimePath(context_->root_dir());
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

  return base::nullopt;
}

void BootPerformanceFetcher::PopulateShutdownInfo(
    mojo_ipc::BootPerformanceInfo* info) {
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

bool BootPerformanceFetcher::ParsePreviousPowerdLog(
    double* shutdown_start_timestamp, std::string* shutdown_reason) {
  const auto& data_path =
      context_->root_dir().Append(kRelativePreviousPowerdLogPath);
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

bool BootPerformanceFetcher::GetShutdownEndTimestamp(
    double* shutdown_end_timestamp) {
  const auto& data_path =
      context_->root_dir().Append(kRelativeShutdownMetricsPath);
  base::File::Info file_info;
  if (!GetFileInfo(data_path, &file_info)) {
    return false;
  }

  *shutdown_end_timestamp = file_info.last_modified.ToDoubleT();

  return true;
}

}  // namespace diagnostics
