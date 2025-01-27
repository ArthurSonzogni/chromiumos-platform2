// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/supplicant/supplicant_eap_state_handler.h"

#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/mock_log.h"
#include "shill/supplicant/wpa_supplicant.h"

using testing::_;
using testing::EndsWith;
using testing::Mock;

namespace shill {

class SupplicantEAPStateHandlerTest : public testing::Test {
 public:
  SupplicantEAPStateHandlerTest()
      : failure_(Service::kFailureNone), metric_(Metrics::kEapEventNoRecords) {}
  ~SupplicantEAPStateHandlerTest() override = default;

 protected:
  void StartEAP() {
    EXPECT_CALL(log_, Log(logging::LOGGING_INFO, _,
                          EndsWith("Authentication starting.")));
    EXPECT_FALSE(handler_.ParseStatus(WPASupplicant::kEAPStatusStarted, "",
                                      &failure_, &metric_));
    Mock::VerifyAndClearExpectations(&log_);
  }

  const std::string& GetTLSError() { return handler_.tls_error_; }

  SupplicantEAPStateHandler handler_;
  Service::ConnectFailure failure_;
  Metrics::EapEvent metric_;
  ScopedMockLog log_;
};

TEST_F(SupplicantEAPStateHandlerTest, Construct) {
  EXPECT_FALSE(handler_.is_eap_in_progress());
  EXPECT_EQ("", GetTLSError());
}

TEST_F(SupplicantEAPStateHandlerTest, AuthenticationStarting) {
  StartEAP();
  EXPECT_TRUE(handler_.is_eap_in_progress());
  EXPECT_EQ("", GetTLSError());
  EXPECT_EQ(Service::kFailureNone, failure_);
  EXPECT_EQ(Metrics::kEapEventAuthAttempt, metric_);
}

TEST_F(SupplicantEAPStateHandlerTest, AcceptedMethod) {
  StartEAP();
  const std::string kEAPMethod("EAP-ROCHAMBEAU");
  EXPECT_CALL(log_, Log(logging::LOGGING_INFO, _,
                        EndsWith("accepted method " + kEAPMethod)));
  EXPECT_FALSE(
      handler_.ParseStatus(WPASupplicant::kEAPStatusAcceptProposedMethod,
                           kEAPMethod, &failure_, &metric_));
  EXPECT_TRUE(handler_.is_eap_in_progress());
  EXPECT_EQ("", GetTLSError());
  EXPECT_EQ(Service::kFailureNone, failure_);
  EXPECT_EQ(Metrics::kEapEventProposedMethodAccepted, metric_);
}

TEST_F(SupplicantEAPStateHandlerTest, SuccessfulCompletion) {
  StartEAP();
  EXPECT_CALL(log_,
              Log(_, _, EndsWith("Completed authentication successfully.")));
  EXPECT_TRUE(handler_.ParseStatus(WPASupplicant::kEAPStatusCompletion,
                                   WPASupplicant::kEAPParameterSuccess,
                                   &failure_, &metric_));
  EXPECT_FALSE(handler_.is_eap_in_progress());
  EXPECT_EQ("", GetTLSError());
  EXPECT_EQ(Service::kFailureNone, failure_);
  EXPECT_EQ(Metrics::kEapEventAuthCompletedSuccess, metric_);
}

TEST_F(SupplicantEAPStateHandlerTest, EAPFailureGeneric) {
  StartEAP();
  // An EAP failure without a previous TLS indication yields a generic failure.
  EXPECT_FALSE(handler_.ParseStatus(WPASupplicant::kEAPStatusCompletion,
                                    WPASupplicant::kEAPParameterFailure,
                                    &failure_, &metric_));

  // Since it hasn't completed successfully, we must assume even in failure
  // that wpa_supplicant is continuing the EAP authentication process.
  EXPECT_TRUE(handler_.is_eap_in_progress());
  EXPECT_EQ("", GetTLSError());
  EXPECT_EQ(Service::kFailureEAPAuthentication, failure_);
  EXPECT_EQ(Metrics::kEapEventAuthFailure, metric_);
}

TEST_F(SupplicantEAPStateHandlerTest, EAPFailureLocalTLSIndication) {
  StartEAP();
  // A TLS indication should be stored but a failure should not be returned.
  EXPECT_FALSE(handler_.ParseStatus(WPASupplicant::kEAPStatusLocalTLSAlert, "",
                                    &failure_, &metric_));
  EXPECT_TRUE(handler_.is_eap_in_progress());
  EXPECT_EQ(WPASupplicant::kEAPStatusLocalTLSAlert, GetTLSError());
  EXPECT_EQ(Service::kFailureNone, failure_);
  EXPECT_EQ(Metrics::kEapEventTlsStatusAlert, metric_);

  // An EAP failure with a previous TLS indication yields a specific failure.
  EXPECT_FALSE(handler_.ParseStatus(WPASupplicant::kEAPStatusCompletion,
                                    WPASupplicant::kEAPParameterFailure,
                                    &failure_, &metric_));
  EXPECT_TRUE(handler_.is_eap_in_progress());
  EXPECT_EQ(Service::kFailureEAPLocalTLS, failure_);
  EXPECT_EQ(Metrics::kEapEventAuthLocalTlsFailure, metric_);
}

TEST_F(SupplicantEAPStateHandlerTest, EAPFailureRemoteTLSIndication) {
  StartEAP();
  // A TLS indication should be stored but a failure should not be returned.
  EXPECT_FALSE(handler_.ParseStatus(WPASupplicant::kEAPStatusRemoteTLSAlert, "",
                                    &failure_, &metric_));
  EXPECT_TRUE(handler_.is_eap_in_progress());
  EXPECT_EQ(WPASupplicant::kEAPStatusRemoteTLSAlert, GetTLSError());
  EXPECT_EQ(Service::kFailureNone, failure_);
  EXPECT_EQ(Metrics::kEapEventTlsStatusAlert, metric_);

  // An EAP failure with a previous TLS indication yields a specific failure.
  EXPECT_FALSE(handler_.ParseStatus(WPASupplicant::kEAPStatusCompletion,
                                    WPASupplicant::kEAPParameterFailure,
                                    &failure_, &metric_));
  EXPECT_TRUE(handler_.is_eap_in_progress());
  EXPECT_EQ(Service::kFailureEAPRemoteTLS, failure_);
  EXPECT_EQ(Metrics::kEapEventAuthRemoteTlsFailure, metric_);
}

TEST_F(SupplicantEAPStateHandlerTest, EAPFailureUnknownParameter) {
  StartEAP();
  const std::string kStrangeParameter("ennui");
  EXPECT_CALL(log_, Log(logging::LOGGING_ERROR, _,
                        EndsWith(std::string("Unexpected ") +
                                 WPASupplicant::kEAPStatusCompletion +
                                 " parameter: " + kStrangeParameter)));
  EXPECT_FALSE(handler_.ParseStatus(WPASupplicant::kEAPStatusCompletion,
                                    kStrangeParameter, &failure_, &metric_));

  // No errors reported, only log error and set metrics.
  EXPECT_TRUE(handler_.is_eap_in_progress());
  EXPECT_EQ("", GetTLSError());
  EXPECT_EQ(Service::kFailureNone, failure_);
  EXPECT_EQ(Metrics::kEapEventUnexpectedFailure, metric_);
}

TEST_F(SupplicantEAPStateHandlerTest, RemoteCertVerificationCompleted) {
  StartEAP();
  EXPECT_CALL(log_,
              Log(logging::LOGGING_INFO, _,
                  EndsWith("Completed remote certificate verification.")));

  EXPECT_FALSE(handler_.ParseStatus(
      WPASupplicant::kEAPStatusRemoteCertificateVerification,
      WPASupplicant::kEAPParameterSuccess, &failure_, &metric_));

  // Although we reported an error, this shouldn't mean failure.
  EXPECT_TRUE(handler_.is_eap_in_progress());
  EXPECT_EQ("", GetTLSError());
  EXPECT_EQ(Service::kFailureNone, failure_);
  EXPECT_EQ(Metrics::kEapEventCertVerificationSuccess, metric_);
}

TEST_F(SupplicantEAPStateHandlerTest, RemoteCertFirstVerificationFailed) {
  StartEAP();
  EXPECT_CALL(log_, Log(logging::LOGGING_ERROR, _,
                        EndsWith("First cert verification failed.")));

  EXPECT_FALSE(handler_.ParseStatus(
      WPASupplicant::kEAPStatusRemoteCertificateVerification,
      WPASupplicant::kEAPCertFirstVerificationFailed, &failure_, &metric_));

  // Although we reported an error, this shouldn't mean failure.
  EXPECT_TRUE(handler_.is_eap_in_progress());
  EXPECT_EQ("", GetTLSError());
  EXPECT_EQ(Service::kFailureNone, failure_);
  EXPECT_EQ(Metrics::kEapEventFirstCertVerificationFailure, metric_);
}

TEST_F(SupplicantEAPStateHandlerTest, RemoteCertVerificationRetryAttempt) {
  StartEAP();
  EXPECT_CALL(
      log_,
      Log(logging::LOGGING_INFO, _,
          EndsWith("retry cert verification with loaded root CA certs.")));

  EXPECT_FALSE(handler_.ParseStatus(
      WPASupplicant::kEAPStatusRemoteCertificateVerification,
      WPASupplicant::kEAPCertRetryVerificationAttempt, &failure_, &metric_));

  EXPECT_TRUE(handler_.is_eap_in_progress());
  EXPECT_EQ("", GetTLSError());
  EXPECT_EQ(Service::kFailureNone, failure_);
  EXPECT_EQ(Metrics::kEapEventCertVerificationRetryAttempt, metric_);
}

TEST_F(SupplicantEAPStateHandlerTest, RemoteCertRetryVerificationFailed) {
  StartEAP();
  EXPECT_CALL(log_, Log(logging::LOGGING_ERROR, _,
                        EndsWith("failed with loaded root CA certs.")));

  EXPECT_FALSE(handler_.ParseStatus(
      WPASupplicant::kEAPStatusRemoteCertificateVerification,
      WPASupplicant::kEAPCertRetryVerificationFailed, &failure_, &metric_));

  // Although we reported an error, this shouldn't mean failure.
  EXPECT_TRUE(handler_.is_eap_in_progress());
  EXPECT_EQ("", GetTLSError());
  EXPECT_EQ(Service::kFailureNone, failure_);
  EXPECT_EQ(Metrics::kEapEventCertVerificationFailureBeforeRetry, metric_);
}

TEST_F(SupplicantEAPStateHandlerTest, RemoteCertAfterRetryVerificationFailed) {
  StartEAP();
  EXPECT_CALL(log_, Log(logging::LOGGING_ERROR, _,
                        EndsWith("verification failed after the retry.")));

  EXPECT_FALSE(handler_.ParseStatus(
      WPASupplicant::kEAPStatusRemoteCertificateVerification,
      WPASupplicant::kEAPCertAfterRetryVerificationFailed, &failure_,
      &metric_));

  // Although we reported an error, this shouldn't mean failure.
  EXPECT_TRUE(handler_.is_eap_in_progress());
  EXPECT_EQ("", GetTLSError());
  EXPECT_EQ(Service::kFailureNone, failure_);
  EXPECT_EQ(Metrics::kEapEventCertVerificationFailureAfterRetry, metric_);
}

TEST_F(SupplicantEAPStateHandlerTest, RemoteCertVerificationFailureAfterRetry) {
  StartEAP();
  EXPECT_CALL(
      log_,
      Log(logging::LOGGING_ERROR, _,
          EndsWith("Failed to load CA certs for cert verification retry.")));

  EXPECT_FALSE(handler_.ParseStatus(
      WPASupplicant::kEAPStatusRemoteCertificateVerification,
      WPASupplicant::kEAPCertLoadForVerificationFailed, &failure_, &metric_));

  // Although we reported an error, this shouldn't mean failure.
  EXPECT_TRUE(handler_.is_eap_in_progress());
  EXPECT_EQ("", GetTLSError());
  EXPECT_EQ(Service::kFailureNone, failure_);
  EXPECT_EQ(Metrics::kEapEventCertVerificationLoadFailure, metric_);
}

TEST_F(SupplicantEAPStateHandlerTest, RemoteCertVerificationIssuerCertAbsent) {
  StartEAP();
  EXPECT_CALL(log_, Log(logging::LOGGING_ERROR, _,
                        EndsWith("Unable to get local issuer certificate.")));

  EXPECT_FALSE(handler_.ParseStatus(
      WPASupplicant::kEAPStatusRemoteCertificateVerification,
      WPASupplicant::kEAPCertVerificationIssuerCertAbsent, &failure_,
      &metric_));

  // Although we reported an error, this shouldn't mean failure.
  EXPECT_TRUE(handler_.is_eap_in_progress());
  EXPECT_EQ("", GetTLSError());
  EXPECT_EQ(Service::kFailureNone, failure_);
  EXPECT_EQ(Metrics::kEapEventCertVerificationIssuerCertAbsent, metric_);
}

TEST_F(SupplicantEAPStateHandlerTest, BadRemoteCertificateVerification) {
  StartEAP();
  const std::string kStrangeParameter("ennui");
  EXPECT_CALL(
      log_,
      Log(logging::LOGGING_ERROR, _,
          EndsWith(std::string("Unexpected ") +
                   WPASupplicant::kEAPStatusRemoteCertificateVerification +
                   " parameter: " + kStrangeParameter)));
  EXPECT_FALSE(handler_.ParseStatus(
      WPASupplicant::kEAPStatusRemoteCertificateVerification, kStrangeParameter,
      &failure_, &metric_));
  // Although we reported an error, this shouldn't mean failure.
  EXPECT_TRUE(handler_.is_eap_in_progress());
  EXPECT_EQ("", GetTLSError());
  EXPECT_EQ(Service::kFailureNone, failure_);
  EXPECT_EQ(Metrics::kEapEventCertVerificationUnexpectedParameter, metric_);
}

TEST_F(SupplicantEAPStateHandlerTest, ParameterNeeded) {
  StartEAP();
  const std::string kAuthenticationParameter("nudge nudge say no more");
  EXPECT_CALL(
      log_,
      Log(logging::LOGGING_ERROR, _,
          EndsWith(
              std::string("aborted due to missing authentication parameter: ") +
              kAuthenticationParameter)));
  EXPECT_FALSE(handler_.ParseStatus(WPASupplicant::kEAPStatusParameterNeeded,
                                    kAuthenticationParameter, &failure_,
                                    &metric_));
  EXPECT_TRUE(handler_.is_eap_in_progress());
  EXPECT_EQ("", GetTLSError());
  EXPECT_EQ(Service::kFailureEAPAuthentication, failure_);
  EXPECT_EQ(Metrics::kEapEventAuthFailurePinMissing, metric_);
}

TEST_F(SupplicantEAPStateHandlerTest, ParameterNeededPin) {
  StartEAP();
  EXPECT_FALSE(handler_.ParseStatus(WPASupplicant::kEAPStatusParameterNeeded,
                                    WPASupplicant::kEAPRequestedParameterPin,
                                    &failure_, &metric_));
  EXPECT_TRUE(handler_.is_eap_in_progress());
  EXPECT_EQ("", GetTLSError());
  EXPECT_EQ(Service::kFailurePinMissing, failure_);
  EXPECT_EQ(Metrics::kEapEventPinMissing, metric_);
}

}  // namespace shill
