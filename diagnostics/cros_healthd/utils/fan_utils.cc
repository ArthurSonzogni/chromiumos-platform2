// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/fan_utils.h"

#include <cstdint>
#include <string>

#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>
#include <brillo/errors/error.h>
#include <re2/re2.h>

namespace diagnostics {

namespace {

using ::chromeos::cros_healthd::mojom::FanInfo;
using ::chromeos::cros_healthd::mojom::FanInfoPtr;

constexpr auto kFanStalledRegex = R"(Fan \d+ stalled!)";
constexpr auto kFanSpeedRegex = R"(Fan \d+ RPM: (\d+))";

}  // namespace

FanFetcher::FanFetcher(org::chromium::debugdProxyInterface* debugd_proxy)
    : debugd_proxy_(debugd_proxy) {
  DCHECK(debugd_proxy_);
}

FanFetcher::~FanFetcher() = default;

std::vector<FanInfoPtr> FanFetcher::FetchFanInfo(
    const base::FilePath& root_dir) {
  std::vector<FanInfoPtr> fan_info;

  // Devices without a Google EC, and therefore ectool, cannot obtain fan info.
  if (!base::PathExists(root_dir.Append(kRelativeCrosEcPath))) {
    LOG(INFO) << "Device does not have a Google EC.";
    return fan_info;
  }

  std::string debugd_result;
  brillo::ErrorPtr error;
  if (!debugd_proxy_->CollectFanSpeed(&debugd_result, &error,
                                      kDebugdDBusTimeout.InMilliseconds())) {
    LOG(ERROR) << "Failed to collect fan speed from debugd: "
               << error->GetCode() << " " << error->GetMessage();
    return fan_info;
  }

  std::vector<std::string> lines = base::SplitString(
      debugd_result, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (const auto& line : lines) {
    if (RE2::FullMatch(line, kFanStalledRegex)) {
      fan_info.push_back(FanInfo::New(0));
      continue;
    }

    std::string regex_result;
    if (!RE2::FullMatch(line, kFanSpeedRegex, &regex_result)) {
      LOG(ERROR) << "Line does not match regex: " << line;
      continue;
    }

    uint32_t speed;
    if (base::StringToUint(regex_result, &speed)) {
      fan_info.push_back(FanInfo::New(speed));
    } else {
      LOG(ERROR) << "Failed to convert regex result to integer: "
                 << regex_result;
    }
  }

  return fan_info;
}

}  // namespace diagnostics
