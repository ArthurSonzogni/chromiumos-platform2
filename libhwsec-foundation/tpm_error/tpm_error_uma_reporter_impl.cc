// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec-foundation/tpm_error/tpm_error_uma_reporter_impl.h"

#include <string>

#include <base/logging.h>

#include "libhwsec-foundation/tpm_error/command_and_response_data.h"
#include "libhwsec-foundation/tpm_error/tpm_error_constants.h"
#include "libhwsec-foundation/tpm_error/tpm_error_data.h"
#include "libhwsec-foundation/tpm_error/tpm_error_metrics_constants.h"

namespace hwsec_foundation {

namespace {

constexpr uint32_t kGscExtensionCC = 0xbaccd00a;
constexpr uint32_t kTpmCcVendorBit = 0x20000000;
constexpr uint32_t kTpmCcVendorGsc = 0x0000;
constexpr uint32_t kGscVendorCC = kTpmCcVendorBit | kTpmCcVendorGsc;

constexpr uint32_t kUnknownVendorSubcmd = 0x0FFF;
constexpr uint32_t kUnknownExtensionSubcmd = 0x0FFF;

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

TpmErrorUmaReporterImpl::TpmErrorUmaReporterImpl(
    MetricsLibraryInterface* metrics)
    : metrics_(metrics) {}

void TpmErrorUmaReporterImpl::Report(const TpmErrorData& data) {
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

bool TpmErrorUmaReporterImpl::ReportCommandAndResponse(
    const std::string& metrics_prefix, const CommandAndResponseData& data) {
  TpmMetricsClientID client_id = GetTpmMetricsClientID();

  std::string client_name = ClientIDToClientName(client_id);
  std::string metrics_name = metrics_prefix + '.' + client_name;
  auto metrics_value = EncodeCommandAndResponse(data);
  if (!metrics_value.has_value()) {
    return false;
  }
  metrics_->SendSparseToUMA(metrics_name, *metrics_value);
  return true;
}

bool TpmErrorUmaReporterImpl::ReportTpm1CommandAndResponse(
    const TpmErrorData& error_data) {
  CommandAndResponseData data{
      .command_type = CommandAndResponseData::CommandType::kGeneric,
      .command = error_data.command,
      .response = error_data.response,
  };
  return ReportCommandAndResponse(kTpm1CommandAndResponsePrefix, data);
}

bool TpmErrorUmaReporterImpl::ReportTpm2CommandAndResponse(
    const TpmErrorData& error_data) {
  CommandAndResponseData data;
  if (error_data.command == kGscVendorCC) {
    data.command_type = CommandAndResponseData::CommandType::kGscVendor;
    // As we are currently not recording the subcmd of vendor cmd, so the subcmd
    // code is always |kUnknownVendorSubcmd|.
    data.command = kUnknownVendorSubcmd;
  } else if (error_data.command == kGscExtensionCC) {
    data.command_type = CommandAndResponseData::CommandType::kGscExtension;
    // As we are currently not recording the subcmd of extension cmd, so the
    // subcmd code is always |kUnknownExtensionSubcmd|.
    data.command = kUnknownExtensionSubcmd;
  } else {
    data.command_type = CommandAndResponseData::CommandType::kGeneric;
    data.command = error_data.command;
  }
  data.response = error_data.response;
  return ReportCommandAndResponse(kTpm2CommandAndResponsePrefix, data);
}

}  // namespace hwsec_foundation
