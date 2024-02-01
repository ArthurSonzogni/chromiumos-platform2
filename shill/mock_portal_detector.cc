// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/mock_portal_detector.h"

#include <utility>

namespace shill {

MockPortalDetector::MockPortalDetector(ResultCallback callback)
    : PortalDetector(nullptr, {}, base::DoNothing()),
      callback_(std::move(callback)) {}

MockPortalDetector::~MockPortalDetector() = default;

void MockPortalDetector::SendResult(const Result& result) {
  callback_.Run(result);
}

MockPortalDetectorFactory::MockPortalDetectorFactory() = default;

MockPortalDetectorFactory::~MockPortalDetectorFactory() = default;

}  // namespace shill
