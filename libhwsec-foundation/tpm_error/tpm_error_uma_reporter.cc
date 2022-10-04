// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec-foundation/tpm_error/tpm_error_uma_reporter.h"

#include <string>

#include <base/logging.h>

#include "libhwsec-foundation/tpm_error/tpm_error_constants.h"
#include "libhwsec-foundation/tpm_error/tpm_error_data.h"
#include "libhwsec-foundation/tpm_error/tpm_error_metrics_constants.h"

namespace hwsec_foundation {

namespace {

TpmMetricsClientID currentTpmMetricsClientID = TpmMetricsClientID::kUnknown;
std::string ClientIDToClientName(TpmMetricsClientID id) {
  switch (id) {
    case TpmMetricsClientID::kUnknown:
      return "Unknown";
    case TpmMetricsClientID::kCryptohome:
      return "Cryptohome";
    case TpmMetricsClientID::kAttestation:
      return "Attestation";
    case TpmMetricsClientID::kTpmManager:
      return "TpmManager";
    case TpmMetricsClientID::kChaps:
      return "Chaps";
    case TpmMetricsClientID::kVtpm:
      return "Vtpm";
    case TpmMetricsClientID::kU2f:
      return "U2f";
    case TpmMetricsClientID::kTrunksSend:
      return "TrunksSend";
  }
}

}  // namespace

void SetTpmMetricsClientID(TpmMetricsClientID id) {
  currentTpmMetricsClientID = id;
}

TpmErrorUmaReporter::TpmErrorUmaReporter(MetricsLibraryInterface* metrics)
    : metrics_(metrics) {}

void TpmErrorUmaReporter::Report(const TpmErrorData& data) {
  switch (data.response) {
    case kTpm1AuthFailResponse:
      metrics_->SendSparseToUMA(kTpm1AuthFailName, data.command);
      break;
    case kTpm1Auth2FailResponse:
      metrics_->SendSparseToUMA(kTpm1Auth2FailName, data.command);
      break;
    default:
      break;
  }
}

bool TpmErrorUmaReporter::ReportCommandAndResponse(
    const std::string& metrics_prefix, const TpmErrorData& data) {
  if (data.command > 0x0FFF || data.response > 0xFFFF ||
      currentTpmMetricsClientID == TpmMetricsClientID::kUnknown) {
    return false;
  }
  std::string client_name = ClientIDToClientName(currentTpmMetricsClientID);
  std::string metrics_name = metrics_prefix + '.' + client_name;
  uint32_t metrics_value = (data.command << 16) + (data.response & 0xFFFF);
  metrics_->SendSparseToUMA(metrics_name, metrics_value);
  return true;
}

bool TpmErrorUmaReporter::ReportTpm2CommandAndResponse(
    const TpmErrorData& data) {
  return ReportCommandAndResponse(kTpm2CommandAndResponsePrefix, data);
}

}  // namespace hwsec_foundation
