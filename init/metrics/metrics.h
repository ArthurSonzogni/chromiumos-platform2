// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_METRICS_METRICS_H_
#define INIT_METRICS_METRICS_H_

#include <string>

#include <base/time/time.h>
#include <metrics/metrics_library.h>

#include "init/tpm_encryption/encryption_key.h"

namespace init_metrics {

// This class provides wrapping functions for callers to report UMAs of
// `metrics`.
class InitMetrics {
 public:
  static void Initialize(const std::string& output_file);
  static InitMetrics* Get();
  static MetricsLibrary* GetInternal();
  static void Reset();

  // Not copyable or movable.
  InitMetrics(const InitMetrics&) = delete;
  InitMetrics& operator=(const InitMetrics&) = delete;
  InitMetrics(InitMetrics&&) = delete;
  InitMetrics& operator=(InitMetrics&&) = delete;

  virtual ~InitMetrics() = default;

  void ReportSystemKeyStatus(encryption::EncryptionKey::SystemKeyStatus status);

  void ReportEncryptionKeyStatus(
      encryption::EncryptionKey::EncryptionKeyStatus status);

 private:
  explicit InitMetrics(const std::string& output_file);

  MetricsLibrary metrics_library_;
};

class ScopedInitMetricsSingleton {
 public:
  explicit ScopedInitMetricsSingleton(const std::string& output_file) {
    InitMetrics::Initialize(output_file);
  }
  ~ScopedInitMetricsSingleton() { InitMetrics::Reset(); }
};

}  // namespace init_metrics

#endif  // INIT_METRICS_METRICS_H_
