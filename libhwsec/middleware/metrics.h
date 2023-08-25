// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_MIDDLEWARE_METRICS_H_
#define LIBHWSEC_MIDDLEWARE_METRICS_H_

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include <metrics/metrics_library.h>

#include "libhwsec/backend/pinweaver.h"
#include "libhwsec/backend/pinweaver_manager/sync_hash_tree_types.h"
#include "libhwsec/status.h"

#ifndef BUILD_LIBHWSEC
#error "Don't include this file outside libhwsec!"
#endif

namespace hwsec {

using LogEntryType = PinWeaver::GetLogResult::LogEntryType;

class Metrics : private MetricsLibrary {
 public:
  Metrics() = default;

  // The constructor for testing purpose.
  explicit Metrics(MetricsLibraryInterface* testing_metrics)
      : metrics_(testing_metrics) {}

  Metrics(const Metrics&) = delete;

  bool SendFuncResultToUMA(const std::string& func_name, const Status& status);

  bool SendPinWeaverSyncOutcomeToUMA(SyncOutcome result);
  bool SendPinWeaverLogReplayResultToUMA(ReplayEntryType type,
                                         LogReplayResult result);
  bool SendPinWeaverReplayOperationResultToUMA(ReplayEntryType replay_type,
                                               LogEntryType entry_type,
                                               const Status& status);

 private:
  MetricsLibraryInterface* metrics_{this};
};

}  // namespace hwsec

#endif  // LIBHWSEC_MIDDLEWARE_METRICS_H_
