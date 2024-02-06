// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "attestation/pca_agent/server/metrics.h"

#include "base/logging.h"

namespace attestation {
namespace pca_agent {

namespace {

// Record the SpaceAvailability when bootlockbox started.
constexpr char kCertificateFetchResult[] =
    "Platform.RksAgent.CertificateFetchResult";

}  // namespace

void Metrics::ReportCertificateFetchResult(CertificateFetchResult result) {
  metrics_library_->SendEnumToUMA(kCertificateFetchResult, result);
}

}  // namespace pca_agent
}  // namespace attestation
