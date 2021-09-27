// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hps/daemon/filters/threshold_filter.h"

namespace hps {

ThresholdFilter::ThresholdFilter(int threshold) : threshold_(threshold) {}

bool ThresholdFilter::ProcessResultImpl(int result) {
  return (result > threshold_);
}

}  // namespace hps
