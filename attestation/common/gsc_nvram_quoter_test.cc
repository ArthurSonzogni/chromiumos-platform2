// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "attestation/common/gsc_nvram_quoter.h"

#include <string>
#include <vector>

#include <attestation/proto_bindings/attestation_ca.pb.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec/frontend/attestation/mock_frontend.h>
#include <libhwsec/structures/space.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <trunks/tpm_utility.h>

using brillo::BlobFromString;
using hwsec::TPMError;
using hwsec::TPMRetryAction;
using hwsec_foundation::error::testing::ReturnError;
using hwsec_foundation::error::testing::ReturnValue;
using testing::_;
using testing::DoAll;
using testing::ElementsAre;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace attestation {

namespace {

constexpr char kFakeSigningKey[] = "signing key";
constexpr char kFakeQuote[] = "quote";
constexpr char kFakeQuotedData[] = "quoted data";

class GscNvramQuoterTest : public ::testing::Test {
 public:
  GscNvramQuoterTest() = default;
  ~GscNvramQuoterTest() override = default;

 protected:
  StrictMock<hwsec::MockAttestationFrontend> mock_hwsec_;
  GscNvramQuoter quoter_{mock_hwsec_};
};

TEST_F(GscNvramQuoterTest, GetListForIdentity) {
  EXPECT_THAT(quoter_.GetListForIdentity(), ElementsAre(BOARD_ID, SN_BITS));
}

TEST_F(GscNvramQuoterTest, GetListForVtpmEkCertificate) {
  EXPECT_THAT(quoter_.GetListForVtpmEkCertificate(), ElementsAre(SN_BITS));
}

TEST_F(GscNvramQuoterTest, GetListForEnrollmentCertificate) {
  EXPECT_THAT(quoter_.GetListForEnrollmentCertificate(),
              ElementsAre(BOARD_ID, SN_BITS, RSU_DEVICE_ID));
}

TEST_F(GscNvramQuoterTest, CertifySuccessBoardId) {
  Quote expected_quote;
  expected_quote.set_quote(kFakeQuote);
  expected_quote.set_quoted_data(kFakeQuotedData);
  EXPECT_CALL(mock_hwsec_, CertifyNV(hwsec::RoSpace::kBoardId,
                                     BlobFromString(kFakeSigningKey)))
      .WillOnce(ReturnValue(expected_quote));

  Quote quote;
  EXPECT_TRUE(quoter_.Certify(BOARD_ID, kFakeSigningKey, quote));
  EXPECT_EQ(quote.quote(), kFakeQuote);
  EXPECT_EQ(quote.quoted_data(), kFakeQuotedData);
}

TEST_F(GscNvramQuoterTest, CertifySuccessSnBits) {
  Quote expected_quote;
  expected_quote.set_quote(kFakeQuote);
  expected_quote.set_quoted_data(kFakeQuotedData);
  EXPECT_CALL(mock_hwsec_, CertifyNV(hwsec::RoSpace::kSNData,
                                     BlobFromString(kFakeSigningKey)))
      .WillOnce(ReturnValue(expected_quote));

  Quote quote;
  EXPECT_TRUE(quoter_.Certify(SN_BITS, kFakeSigningKey, quote));
  EXPECT_EQ(quote.quote(), kFakeQuote);
  EXPECT_EQ(quote.quoted_data(), kFakeQuotedData);
}

TEST_F(GscNvramQuoterTest, CertifySuccessRsuDevceId) {
  Quote expected_quote;
  expected_quote.set_quote(kFakeQuote);
  expected_quote.set_quoted_data(kFakeQuotedData);
  EXPECT_CALL(mock_hwsec_, CertifyNV(hwsec::RoSpace::kRsuDeviceId,
                                     BlobFromString(kFakeSigningKey)))
      .WillOnce(ReturnValue(expected_quote));

  Quote quote;
  EXPECT_TRUE(quoter_.Certify(RSU_DEVICE_ID, kFakeSigningKey, quote));
  EXPECT_EQ(quote.quote(), kFakeQuote);
  EXPECT_EQ(quote.quoted_data(), kFakeQuotedData);
}

TEST_F(GscNvramQuoterTest, CertifySuccessRsaEkCertificate) {
  Quote expected_quote;
  expected_quote.set_quote(kFakeQuote);
  expected_quote.set_quoted_data(kFakeQuotedData);
  EXPECT_CALL(mock_hwsec_, CertifyNV(hwsec::RoSpace::kEndorsementRsaCert,
                                     BlobFromString(kFakeSigningKey)))
      .WillOnce(ReturnValue(expected_quote));

  Quote quote;
  EXPECT_TRUE(quoter_.Certify(RSA_PUB_EK_CERT, kFakeSigningKey, quote));
  EXPECT_EQ(quote.quote(), kFakeQuote);
  EXPECT_EQ(quote.quoted_data(), kFakeQuotedData);
}

TEST_F(GscNvramQuoterTest, CertifyFailure) {
  Quote expected_quote;
  expected_quote.set_quote(kFakeQuote);
  expected_quote.set_quoted_data(kFakeQuotedData);
  EXPECT_CALL(mock_hwsec_, CertifyNV(hwsec::RoSpace::kSNData,
                                     BlobFromString(kFakeSigningKey)))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  Quote quote;
  EXPECT_FALSE(quoter_.Certify(SN_BITS, kFakeSigningKey, quote));
}

}  // namespace

}  // namespace attestation
