// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <vector>

#include "hps/daemon/filters/filter_watcher.h"

namespace hps {
namespace {

// Returns a serialzed bytes of HpsResultProto that contains the `result`.
std::vector<uint8_t> HpsResultToSerializedBytes(HpsResult result) {
  HpsResultProto result_proto;
  result_proto.set_value(result);

  std::vector<uint8_t> serialized;
  serialized.resize(result_proto.ByteSizeLong());
  result_proto.SerializeToArray(serialized.data(),
                                static_cast<int>(serialized.size()));
  return serialized;
}

}  // namespace

FilterWatcher::FilterWatcher(std::unique_ptr<Filter> wrapped_filter,
                             StatusCallback signal)
    : wrapped_filter_(std::move(wrapped_filter)),
      status_changed_callback_(std::move(signal)) {}

HpsResult FilterWatcher::ProcessResultImpl(int result, bool valid) {
  auto previous_filter_result = wrapped_filter_->GetCurrentResult();
  auto filter_result = wrapped_filter_->ProcessResult(result, valid);

  if (filter_result != previous_filter_result) {
    status_changed_callback_.Run(HpsResultToSerializedBytes(filter_result));
  }

  return filter_result;
}

}  // namespace hps
