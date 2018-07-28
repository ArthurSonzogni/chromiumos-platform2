// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_SETUP_ARC_SETUP_METRICS_H_
#define ARC_SETUP_ARC_SETUP_METRICS_H_

#include <memory>
#include <string>

#include <base/macros.h>
#include <base/time/time.h>

class MetricsLibraryInterface;

namespace arc {

// Enum is append only and need to match the definition in
// Chromium's src/tools/metrics/histograms/enums.xml.
enum class ArcBootContinueCodeInstallationResult {
  SUCCESS = 0,
  ERROR_HOST_SIDE_CODE_NOT_READY = 1,
  ERROR_CANNOT_INSTALL_HOST_CODE = 2,
  COUNT
};

// Enum is append only and need to match the definition in
// Chromium's src/tools/metrics/histograms/enums.xml.
enum class ArcCodeRelocationResult {
  SUCCESS = 0,
  ERROR_BOOTLOCKBOXD_NOT_READY = 1,
  ERROR_UNABLE_TO_RELOCATE = 2,
  ERROR_UNABLE_TO_SIGN = 3,
  SALT_EMPTY = 4,
  COUNT
};

// Enum is append only and need to match the definition in
// Chromium's src/tools/metrics/histograms/enums.xml.
enum class ArcCodeVerificationResult {
  SUCCESS = 0,
  ERROR_BOOTLOCKBOXD_NOT_READY = 1,
  OTA = 2,
  INVALID_CODE = 3,
  COUNT
};

// A class that sends UMA metrics using MetricsLibrary. There is no D-Bus call
// because MetricsLibrary writes the UMA data to /var/lib/metrics/uma-events.
class ArcSetupMetrics {
 public:
  ArcSetupMetrics();
  ~ArcSetupMetrics() = default;

  // Sends host code verification result.
  bool SendCodeVerificationResult(
      ArcCodeVerificationResult verification_result);

  // Sends host code relocation result.
  bool SendCodeRelocationResult(ArcCodeRelocationResult relocation_result);

  // Sends the time verifying host generated code.
  bool SendCodeVerificationTime(base::TimeDelta verificaiton_time);

  // Sends the time relocating android boot*.art code.
  bool SendCodeRelocationTime(base::TimeDelta relocation_time);

  // Sends boot-continue host code installation results.
  bool SendBootContinueCodeInstallationResult(
      ArcBootContinueCodeInstallationResult verification_result);

  // Sends host code signing time using TPM bootlockbox.
  bool SendCodeSigningTime(base::TimeDelta signing_time);

  // Sends total time on host code integrity checking. This includes time on
  // verification. And also the time on relocation and signing if verification
  // fails.
  bool SendCodeIntegrityCheckingTotalTime(base::TimeDelta total_time);

  void SetMetricsLibraryForTesting(
      std::unique_ptr<MetricsLibraryInterface> metrics_library);

  MetricsLibraryInterface* metrics_library_for_testing() {
    return metrics_library_.get();
  }

 private:
  bool SendDurationToUMA(const std::string& metric_name,
                         base::TimeDelta duration);

  std::unique_ptr<MetricsLibraryInterface> metrics_library_;

  DISALLOW_COPY_AND_ASSIGN(ArcSetupMetrics);
};

}  // namespace arc

#endif  // ARC_SETUP_ARC_SETUP_METRICS_H_
