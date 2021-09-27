// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hps/daemon/filters/filter_factory.h"

#include <memory>
#include <utility>

#include "hps/daemon/filters/filter_watcher.h"
#include "hps/daemon/filters/threshold_filter.h"

namespace hps {

// TODO(slangley): This needs confirming from MI team.
constexpr uint8_t kDefaultThreshold = 127;

std::unique_ptr<Filter> CreateFilter(const hps::FeatureConfig& config,
                                     StatusCallback signal) {
  std::unique_ptr<Filter> filter;

  switch (config.filter_config_case()) {
    case FeatureConfig::kBasicFilterConfig:
    case FeatureConfig::FILTER_CONFIG_NOT_SET:
      filter = std::make_unique<ThresholdFilter>(kDefaultThreshold);
      break;
    default:
      LOG(FATAL) << "Unexpected config";
  }
  return std::make_unique<FilterWatcher>(std::move(filter), std::move(signal));
}

}  // namespace hps
