// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hermes/lpa_util.h"

#include <base/location.h>
#include <brillo/errors/error.h>
#include <brillo/errors/error_codes.h>
#include <chromeos/dbus/service_constants.h>
#include <google-lpa/lpa/card/euicc_card.h>
#include <google-lpa/lpa/core/lpa.h>
#include <google-lpa/lpa/smdx/smdp_client.h>
#include <gtest/gtest.h>

namespace hermes {

TEST(LpaUtilTest, NoErrorReturnsNullptr) {
  EXPECT_EQ(LpaErrorToBrillo(FROM_HERE, lpa::core::Lpa::kNoError), nullptr);
  EXPECT_EQ(LpaErrorToBrillo(FROM_HERE, lpa::core::Lpa::kDownloadProfileError +
                                            lpa::core::Lpa::kNoError),
            nullptr);
}

TEST(LpaUtilTest, ConfirmationCodeMissingVsRefusedMappings) {
  auto missing_err = LpaErrorToBrillo(
      FROM_HERE, lpa::core::Lpa::kDownloadProfileError +
                     lpa::smdp::SmdpClient::kGetBoundProfilePackageError +
                     lpa::smdp::SmdpClient::kConfirmationCodeMissing);
  ASSERT_NE(missing_err, nullptr);
  EXPECT_EQ(missing_err->GetDomain(), brillo::errors::dbus::kDomain);
  EXPECT_EQ(missing_err->GetCode(), kErrorNeedConfirmationCode);
  EXPECT_EQ(missing_err->GetMessage(),
            "Lpa DownloadProfile: Smdp GetBoundProfilePackage: Confirmation "
            "code missing (305)");

  auto refused_err = LpaErrorToBrillo(
      FROM_HERE, lpa::core::Lpa::kDownloadProfileError +
                     lpa::smdp::SmdpClient::kGetBoundProfilePackageError +
                     lpa::smdp::SmdpClient::kConfirmationCodeRefused);
  ASSERT_NE(refused_err, nullptr);
  EXPECT_EQ(refused_err->GetDomain(), brillo::errors::dbus::kDomain);
  EXPECT_EQ(refused_err->GetCode(), kErrorMalformedResponse);
  EXPECT_EQ(refused_err->GetMessage(),
            "Lpa DownloadProfile: Smdp GetBoundProfilePackage: Confirmation "
            "code refused by SM-DP+ (304)");

  auto local_cc_err =
      LpaErrorToBrillo(FROM_HERE, lpa::core::Lpa::kDownloadProfileError +
                                      lpa::core::Lpa::kNeedConfirmationCode);
  ASSERT_NE(local_cc_err, nullptr);
  EXPECT_EQ(local_cc_err->GetDomain(), brillo::errors::dbus::kDomain);
  EXPECT_EQ(local_cc_err->GetCode(), kErrorNeedConfirmationCode);
  EXPECT_EQ(local_cc_err->GetMessage(),
            "Lpa DownloadProfile: Need confirmation code (5)");

  auto malformed_err = LpaErrorToBrillo(
      FROM_HERE, lpa::core::Lpa::kDownloadProfileError +
                     lpa::smdp::SmdpClient::kGetBoundProfilePackageError +
                     lpa::smdp::SmdpClient::kMalformedResponse);
  ASSERT_NE(malformed_err, nullptr);
  EXPECT_EQ(malformed_err->GetCode(), kErrorMalformedResponse);
  EXPECT_EQ(malformed_err->GetMessage(),
            "Lpa DownloadProfile: Smdp GetBoundProfilePackage: Malformed "
            "response (300)");
}

TEST(LpaUtilTest, PprNotSupportedDoesNotCollideWithCardErrors) {
  auto ppr_err =
      LpaErrorToBrillo(FROM_HERE, lpa::core::Lpa::kDownloadProfileError +
                                      lpa::core::Lpa::kPprNotSupported);
  ASSERT_NE(ppr_err, nullptr);
  EXPECT_EQ(ppr_err->GetDomain(), brillo::errors::dbus::kDomain);
  EXPECT_EQ(ppr_err->GetCode(), kErrorUnsupported);
  EXPECT_EQ(ppr_err->GetMessage(),
            "Lpa DownloadProfile: Profile policy rules not allowed (10)");

  // SGP.22 LoadBoundProfilePackage ErrorReason = 10
  // (installFailedDueToInsufficientMemoryForProfile) must NOT collide with
  // Lpa::kPprNotSupported (10).
  auto oom_err = LpaErrorToBrillo(
      FROM_HERE, lpa::core::Lpa::kDownloadProfileError +
                     lpa::card::EuiccCard::kLoadBoundProfilePackageError + 10);
  ASSERT_NE(oom_err, nullptr);
  EXPECT_EQ(oom_err->GetDomain(), brillo::errors::dbus::kDomain);
  EXPECT_EQ(oom_err->GetCode(), kErrorUnknown);
  EXPECT_EQ(oom_err->GetMessage(),
            "Lpa DownloadProfile: EuiccCard LoadBoundProfilePackage: Unknown "
            "error (10)");
}

TEST(LpaUtilTest, AuthenticateServerAndUnknownFallbackMappings) {
  auto auth_server_err = LpaErrorToBrillo(
      FROM_HERE, lpa::core::Lpa::kDownloadProfileError +
                     lpa::card::EuiccCard::kAuthenticateServerError + 4);
  ASSERT_NE(auth_server_err, nullptr);
  EXPECT_EQ(auth_server_err->GetCode(), kErrorMalformedResponse);
  EXPECT_EQ(auth_server_err->GetMessage(),
            "Lpa DownloadProfile: EuiccCard AuthenticateServer: Malformed "
            "response from eUICC. Invalid activation code may have been "
            "reused (4)");

  auto unknown_err =
      LpaErrorToBrillo(FROM_HERE, lpa::core::Lpa::kDownloadProfileError + 999);
  ASSERT_NE(unknown_err, nullptr);
  EXPECT_EQ(unknown_err->GetCode(), kErrorUnknown);
  EXPECT_EQ(unknown_err->GetMessage(),
            "Lpa DownloadProfile: Unknown error (999)");

  // ProfileDownloadTransaction::Authenticate emits SmdpClient::kProfileBuild
  // (303) with lpa_mid_code == 0 when BuildProfile() rejects malformed
  // profileMetadata (e.g. strict DER 0x99 profilePolicyRules).
  auto profile_build_err =
      LpaErrorToBrillo(FROM_HERE, lpa::core::Lpa::kDownloadProfileError +
                                      lpa::smdp::SmdpClient::kProfileBuild);
  ASSERT_NE(profile_build_err, nullptr);
  EXPECT_EQ(profile_build_err->GetDomain(), brillo::errors::dbus::kDomain);
  EXPECT_EQ(profile_build_err->GetCode(), kErrorMalformedResponse);
  EXPECT_EQ(profile_build_err->GetMessage(),
            "Lpa DownloadProfile: Failed to build profile (303)");
}

}  // namespace hermes
