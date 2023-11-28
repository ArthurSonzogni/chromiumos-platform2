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

#include "libhwsec/backend/pinweaver_manager/sync_hash_tree_types.h"
#include "libhwsec/error/tpm_retry_action.h"
#include "libhwsec/status.h"

namespace {
constexpr char kHwsecMetricsPrefix[] = "Platform.Libhwsec.RetryAction.";
constexpr size_t kHwsecMetricsPrefixLength = sizeof(kHwsecMetricsPrefix) - 1;
constexpr char kPinWeaverSyncMetricsPrefix[] =
    "Platform.Libhwsec.PinWeaverManager.SyncHashTree.";
constexpr char kPinWeaverReplayTypeNormal[] = ".Normal";
constexpr char kPinWeaverReplayTypeFull[] = ".Full";
}  // namespace

namespace hwsec {

const char* GetPinWeaverReplayEntryTypeName(ReplayEntryType type) {
  switch (type) {
    case ReplayEntryType::kNormal:
      return "Normal";
    case ReplayEntryType::kMismatchedHash:
      return "MismatchedHash";
    case ReplayEntryType::kSecondEntry:
      return "SecondEntry";
  }
}

const char* GetPinWeaverLogEntryTypeName(LogEntryType type) {
  switch (type) {
    case LogEntryType::kInsert:
      return "ReplayInsert";
    case LogEntryType::kCheck:
      return "ReplayCheck";
    case LogEntryType::kRemove:
      return "ReplayRemove";
    case LogEntryType::kReset:
      return "ReplayReset";
    case LogEntryType::kInvalid:
      return "ReplayInvalid";
  }
}

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

bool Metrics::SendPinWeaverSyncOutcomeToUMA(SyncOutcome result) {
  std::string current_uma =
      std::string(kPinWeaverSyncMetricsPrefix) + "SyncOutcome";
  return metrics_->SendEnumToUMA(current_uma, result);
}

bool Metrics::SendPinWeaverLogReplayResultToUMA(ReplayEntryType type,
                                                LogReplayResult result) {
  std::string current_uma =
      std::string(kPinWeaverSyncMetricsPrefix) + "ReplayLogResult";
  bool ret = metrics_->SendEnumToUMA(current_uma, result);

  // Report again specifying whether it's a full replay.
  if (type == ReplayEntryType::kNormal) {
    current_uma.append(kPinWeaverReplayTypeNormal);
  } else {
    // ReplayEntryType::kMismatchedHash/kSecondEntry happens during full replay.
    current_uma.append(kPinWeaverReplayTypeFull);
  }
  return ret && metrics_->SendEnumToUMA(current_uma, result);
}

bool Metrics::SendPinWeaverReplayOperationResultToUMA(
    ReplayEntryType replay_type,
    LogEntryType entry_type,
    const Status& status) {
  std::string hist_str = std::string("PinWeaverManager.") +
                         GetPinWeaverLogEntryTypeName(entry_type) + "." +
                         GetPinWeaverReplayEntryTypeName(replay_type);
  return SendFuncResultToUMA(hist_str, status);
}

}  // namespace hwsec
