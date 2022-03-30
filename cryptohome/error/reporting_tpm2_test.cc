// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>
#include <set>
#include <utility>

#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <gtest/gtest.h>
#include <libhwsec/error/tpm_error.h>
#include <libhwsec/error/tpm2_error.h>
#include <libhwsec-foundation/status/status_chain.h>
#include <metrics/metrics_library_mock.h>

#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/error/converter.h"
#include "cryptohome/error/cryptohome_tpm_error.h"
#include "cryptohome/error/reporting.h"

namespace cryptohome {

namespace error {

namespace {

using testing::_;
using testing::Return;
using testing::StrictMock;

using hwsec::TPM2Error;
using hwsec::TPMError;
using hwsec::unified_tpm_error::kUnifiedErrorBit;
using hwsec_foundation::error::CreateError;
using hwsec_foundation::error::WrapError;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::StatusChain;

class ErrorReportingTpm2Test : public ::testing::Test {
 public:
  ErrorReportingTpm2Test() = default;

  void SetUp() override { OverrideMetricsLibraryForTesting(&metrics_); }

  void TearDown() override { ClearMetricsLibraryForTesting(); }

 protected:
  StrictMock<MetricsLibraryMock> metrics_;
};

constexpr CryptohomeError::ErrorLocation kErrorLocationForTesting1 =
    static_cast<::cryptohome::error::CryptohomeError::ErrorLocation>(1);

constexpr trunks::TPM_RC kTestingTpmError1 = trunks::TPM_RC_LOCKOUT;

TEST_F(ErrorReportingTpm2Test, SimpleTPM2Error) {
  // Setup the expected result.
  EXPECT_CALL(metrics_,
              SendSparseToUMA(std::string(kCryptohomeErrorAllLocations),
                              kErrorLocationForTesting1))
      .WillOnce(Return(true));
  EXPECT_CALL(metrics_,
              SendSparseToUMA(std::string(kCryptohomeErrorAllLocations),
                              static_cast<CryptohomeError::ErrorLocation>(
                                  kTestingTpmError1) |
                                  kUnifiedErrorBit))
      .WillOnce(Return(true));
  // HashedStack value is precomputed.
  EXPECT_CALL(
      metrics_,
      SendSparseToUMA(std::string(kCryptohomeErrorHashedStack), -1721192113))
      .WillOnce(Return(true));

  // Generate the mixed TPM error.
  CryptohomeError::ErrorLocation mixed =
      static_cast<CryptohomeError::ErrorLocation>(kTestingTpmError1) |
      (kErrorLocationForTesting1 << 16);
  EXPECT_CALL(metrics_,
              SendSparseToUMA(std::string(kCryptohomeErrorLeafWithTPM), mixed))
      .WillOnce(Return(true));

  // Setup the errors.
  auto err1 = CreateError<TPM2Error>(kTestingTpmError1);
  auto err2 = WrapError<TPMError>(std::move(err1), "Testing1");
  auto err3 = MakeStatus<CryptohomeTPMError>(std::move(err2));

  auto err4 =
      MakeStatus<CryptohomeError>(kErrorLocationForTesting1, NoErrorAction(),
                                  user_data_auth::CryptohomeErrorCode::
                                      CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND)
          .Wrap(std::move(err3));

  user_data_auth::CryptohomeErrorCode legacy_ec;
  user_data_auth::CryptohomeErrorInfo info =
      CryptohomeErrorToUserDataAuthError(err4, &legacy_ec);

  // Make the call.
  ReportCryptohomeError(err4, info);
}

}  // namespace

}  // namespace error

}  // namespace cryptohome
