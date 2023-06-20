// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/middleware/metrics.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/strings/string_split.h>
#include <metrics/metrics_library.h>

#include "libhwsec/error/tpm_retry_action.h"
#include "libhwsec/status.h"

namespace {
constexpr char kHwsecMetricsPrefix[] = "Platform.Libhwsec.RetryAction.";
constexpr size_t kHwsecMetricsPrefixLength = sizeof(kHwsecMetricsPrefix) - 1;
}  // namespace

namespace hwsec {

bool Metrics::SendFuncResultToUMA(const std::string& func_name,
                                  const Status& status) {
  TPMRetryAction action;
  if (status.ok()) {
    action = TPMRetryAction::kNone;
  } else {
    action = status->ToTPMRetryAction();
  }

  std::string current_uma = kHwsecMetricsPrefix + func_name;

  bool result = true;

  while (current_uma.size() > kHwsecMetricsPrefixLength) {
    result &= metrics_->SendEnumToUMA(current_uma, action);

    size_t pos = current_uma.find_last_of('.');
    CHECK_NE(pos, std::string::npos);
    current_uma.resize(pos);
  }

  return result;
}

}  // namespace hwsec
