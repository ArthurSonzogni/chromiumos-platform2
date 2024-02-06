// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ATTESTATION_PCA_AGENT_SERVER_METRICS_H_
#define ATTESTATION_PCA_AGENT_SERVER_METRICS_H_

#include <metrics/metrics_library.h>

namespace attestation {
namespace pca_agent {

// The status of fetching the certificate XML files from the server.
enum class CertificateFetchResult {
  kSuccess = 0,
  // Failed to fetch the certificate files from server. If needed, we'll break
  // this into more buckets in the future.
  kFailed = 1,
  kMaxValue = kFailed,
};

class Metrics : private MetricsLibrary {
 public:
  Metrics() = default;
  Metrics(const Metrics&) = delete;
  Metrics& operator=(const Metrics&) = delete;

  ~Metrics() = default;

  void ReportCertificateFetchResult(CertificateFetchResult result);

 private:
  MetricsLibraryInterface* metrics_library_{this};
};

}  // namespace pca_agent
}  // namespace attestation

#endif  // ATTESTATION_PCA_AGENT_SERVER_METRICS_H_
