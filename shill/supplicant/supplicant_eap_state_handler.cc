// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/supplicant/supplicant_eap_state_handler.h"

#include <base/logging.h>
#include <shill/metrics.h>

#include "shill/logging.h"
#include "shill/supplicant/wpa_supplicant.h"

namespace shill {

SupplicantEAPStateHandler::SupplicantEAPStateHandler()
    : is_eap_in_progress_(false) {}

SupplicantEAPStateHandler::~SupplicantEAPStateHandler() = default;

bool SupplicantEAPStateHandler::ParseStatus(
    const std::string& status,
    const std::string& parameter,
    Service::ConnectFailure* failure,
    Metrics::EapEvent* metrics_eap_event) {
  CHECK(metrics_eap_event);
  if (status == WPASupplicant::kEAPStatusAcceptProposedMethod) {
    LOG(INFO) << "EAP: accepted method " << parameter;
    // Added for CA cert verification experiment b:381389348.
    *metrics_eap_event = Metrics::kEapEventProposedMethodAccepted;
  } else if (status == WPASupplicant::kEAPStatusCompletion) {
    if (parameter == WPASupplicant::kEAPParameterSuccess) {
      LOG(INFO) << "EAP: Completed authentication successfully.";
      is_eap_in_progress_ = false;
      *metrics_eap_event = Metrics::kEapEventAuthCompletedSuccess;
      return true;
    } else if (parameter == WPASupplicant::kEAPParameterFailure) {
      // If there was a TLS error, use this instead of the generic failure.
      if (tls_error_ == WPASupplicant::kEAPStatusLocalTLSAlert) {
        *failure = Service::kFailureEAPLocalTLS;
        *metrics_eap_event = Metrics::kEapEventAuthLocalTlsFailure;
      } else if (tls_error_ == WPASupplicant::kEAPStatusRemoteTLSAlert) {
        *failure = Service::kFailureEAPRemoteTLS;
        *metrics_eap_event = Metrics::kEapEventAuthRemoteTlsFailure;
      } else {
        *failure = Service::kFailureEAPAuthentication;
        *metrics_eap_event = Metrics::kEapEventAuthFailure;
      }
    } else {
      LOG(ERROR) << "EAP: Unexpected " << status << " parameter: " << parameter;
      *metrics_eap_event = Metrics::kEapEventUnexpectedFailure;
    }
  } else if (status == WPASupplicant::kEAPStatusLocalTLSAlert ||
             status == WPASupplicant::kEAPStatusRemoteTLSAlert) {
    tls_error_ = status;
    *metrics_eap_event = Metrics::kEapEventTlsStatusAlert;
  } else if (status == WPASupplicant::kEAPStatusRemoteCertificateVerification) {
    if (parameter == WPASupplicant::kEAPParameterSuccess) {
      LOG(INFO) << "EAP: Completed remote certificate verification.";
      // Added for CA cert verification experiment b:381389348.
      *metrics_eap_event = Metrics::kEapEventCertVerificationSuccess;
    } else if (parameter == WPASupplicant::kEAPCertFirstVerificationFailed) {
      LOG(ERROR) << "EAP: First cert verification failed.";
      // Added for CA cert verification experiment b:381389348.
      *metrics_eap_event = Metrics::kEapEventFirstCertVerificationFailure;
    } else if (parameter == WPASupplicant::kEAPCertRetryVerificationAttempt) {
      LOG(INFO)
          << "Attempt to retry cert verification with loaded root CA certs.";
      // Added for CA cert verification experiment b:381389348.
      *metrics_eap_event = Metrics::kEapEventCertVerificationRetryAttempt;
    } else if (parameter == WPASupplicant::kEAPCertRetryVerificationFailed) {
      LOG(ERROR) << "EAP: Cert verification failed with loaded root CA certs.";
      // Added for CA cert verification experiment b:381389348.
      *metrics_eap_event = Metrics::kEapEventCertVerificationFailureBeforeRetry;
    } else if (parameter ==
               WPASupplicant::kEAPCertAfterRetryVerificationFailed) {
      LOG(ERROR) << "EAP: Cert verification failed after the retry.";
      *metrics_eap_event = Metrics::kEapEventCertVerificationFailureAfterRetry;
    } else if (parameter == WPASupplicant::kEAPCertLoadForVerificationFailed) {
      LOG(ERROR) << "EAP: Failed to load CA certs for cert verification retry.";
      // Added for CA cert verification experiment b:381389348.
      *metrics_eap_event = Metrics::kEapEventCertVerificationLoadFailure;
    } else if (parameter ==
               WPASupplicant::kEAPCertVerificationIssuerCertAbsent) {
      LOG(ERROR) << "EAP: Unable to get local issuer certificate.";
      *metrics_eap_event = Metrics::kEapEventCertVerificationIssuerCertAbsent;
    } else {
      // wpa_supplicant doesn't currently have a verification failure
      // message.  We will instead get a RemoteTLSAlert above.
      LOG(ERROR) << "EAP: Unexpected " << status << " parameter: " << parameter;
      *metrics_eap_event =
          Metrics::kEapEventCertVerificationUnexpectedParameter;
    }
  } else if (status == WPASupplicant::kEAPStatusParameterNeeded) {
    if (parameter == WPASupplicant::kEAPRequestedParameterPin) {
      // wpa_supplicant could have erased the PIN.  Signal to WiFi that
      // it should supply one if possible.
      *failure = Service::kFailurePinMissing;
      *metrics_eap_event = Metrics::kEapEventPinMissing;
    } else {
      LOG(ERROR) << "EAP: Authentication aborted due to missing authentication "
                 << "parameter: " << parameter;
      *failure = Service::kFailureEAPAuthentication;
      *metrics_eap_event = Metrics::kEapEventAuthFailurePinMissing;
    }
  } else if (status == WPASupplicant::kEAPStatusStarted) {
    LOG(INFO) << "EAP: Authentication starting.";
    is_eap_in_progress_ = true;
    *metrics_eap_event = Metrics::kEapEventAuthAttempt;
  }

  return false;
}

void SupplicantEAPStateHandler::Reset() {
  is_eap_in_progress_ = false;
  tls_error_ = "";
}

}  // namespace shill
