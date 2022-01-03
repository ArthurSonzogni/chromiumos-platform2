// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>
#include <vector>

#include <base/strings/stringprintf.h>
#include <re2/re2.h>

#include "diagnostics/cros_healthd/fetchers/display_fetcher.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"

namespace diagnostics {

namespace {

namespace executor_ipc = chromeos::cros_healthd_executor::mojom;
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

void PopulatePrivacyScreenInfo(const std::string& result,
                               bool* supported,
                               bool* enabled) {
  *supported = result.find("privacy-screen") != std::string::npos;
  *enabled = false;
  if (!*supported) {
    return;
  }

  // TODO(kerker) Maybe we should parse the whole output to retrieve more info.
  // But it depends on the request. Moreover, some info can be retrieved from
  // EDID, we need to consider this two data sources according to the request in
  // the future.
  std::string content = result;

  // Find the first eDP section start point.
  auto edp_pos = result.find("eDP");

  // From eDP section, we find the next status(*) position, it must be next
  // connector section.
  // (*) status is "connected" or "disconnected".
  auto next_section_pos = result.find("connected", edp_pos);
  content = content.substr(edp_pos, next_section_pos - edp_pos);

  // Find the privacy screen field position.
  auto privacy_screen_pos = content.find("privacy-screen");
  content = content.substr(privacy_screen_pos);

  // Find the first "value" after privacy screen section.
  auto value_pos = content.find("value");
  content = content.substr(value_pos);

  std::string value;
  const char regex[] = R"(value: (.*)\n.*)";
  RE2::FullMatch(content, regex, &value);

  *enabled = value == "1";
}

void FinishFetchingDisplayInfo(
    base::OnceCallback<void(mojo_ipc::DisplayResultPtr)> callback,
    executor_ipc::ProcessResultPtr result) {
  std::string err = result->err;
  int32_t return_code = result->return_code;
  if (!err.empty() || return_code != EXIT_SUCCESS) {
    std::move(callback).Run(
        mojo_ipc::DisplayResult::NewError(CreateAndLogProbeError(
            mojo_ipc::ErrorType::kSystemUtilityError,
            base::StringPrintf(
                "RunModetest failed with return code: %d and error: %s",
                return_code, err.c_str()))));
    return;
  }

  auto display_info = mojo_ipc::DisplayInfo::New();

  // EmbeddedDisplayInfo
  auto edp_info = mojo_ipc::EmbeddedDisplayInfo::New();
  PopulatePrivacyScreenInfo(result->out, &edp_info->privacy_screen_supported,
                            &edp_info->privacy_screen_enabled);
  display_info->edp_info = std::move(edp_info);

  std::move(callback).Run(
      mojo_ipc::DisplayResult::NewDisplayInfo(std::move(display_info)));
}

}  // namespace

void DisplayFetcher::FetchDisplayInfo(
    DisplayFetcher::FetchDisplayInfoCallback&& callback) {
  auto libdrm_util = context_->CreateLibdrmUtil();
  context_->executor()->RunModetest(
      executor_ipc::ModetestOptionEnum::kListConnector,
      base::BindOnce(&FinishFetchingDisplayInfo, std::move(callback)));
}

}  // namespace diagnostics
