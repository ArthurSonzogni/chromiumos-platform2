// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CORAL_COMMON_H_
#define ODML_CORAL_COMMON_H_

#include <memory>
#include <vector>

#include <base/time/time.h>
#include <base/types/expected.h>

#include "odml/mojom/coral_service.mojom.h"

namespace coral {

template <class T>
using CoralResult = base::expected<T, mojom::CoralError>;
using CoralStatus = CoralResult<void>;

// Used as parent struct of Response types, which we want to enforce move-only.
struct MoveOnly {
  MoveOnly() = default;
  MoveOnly(const MoveOnly&) = delete;
  MoveOnly(MoveOnly&&) = default;
  MoveOnly& operator=(MoveOnly&&) = default;
  bool operator==(const MoveOnly&) const = default;
};

using Embedding = std::vector<float>;

struct EmbeddingEntry {
  Embedding embedding;
  // The safety verdict of the entry. True means pass, and false means fail.
  std::optional<bool> safety_verdict;
  bool operator==(const EmbeddingEntry&) const = default;
};

class PerformanceTimer {
 public:
  using Ptr = std::unique_ptr<PerformanceTimer>;

  PerformanceTimer();
  PerformanceTimer(const PerformanceTimer&) = delete;

  static Ptr Create();

  // Get the duration that has passed since `start_time_`.
  base::TimeDelta GetDuration() const;

 private:
  base::TimeTicks start_time_;
};

}  // namespace coral

#endif  // ODML_CORAL_COMMON_H_
