// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm1/backend_test_base.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <absl/base/attributes.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/crypto/openssl.h>
#include <libhwsec-foundation/crypto/rsa.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client-test/tpm_manager/dbus-proxy-mocks.h>

#include "libhwsec/backend/tpm1/backend.h"
#include "libhwsec/error/tpm1_error.h"
#include "libhwsec/middleware/middleware_derivative.h"
#include "libhwsec/middleware/middleware_owner.h"
#include "libhwsec/overalls/mock_overalls.h"
#include "libhwsec/proxy/proxy_for_test.h"
#include "libhwsec/status.h"

using brillo::BlobFromString;
using hwsec_foundation::error::testing::IsOkAndHolds;
using testing::_;
using testing::Args;
using testing::AtMost;
using testing::DoAll;
using testing::ElementsAreArray;
using testing::Return;
using testing::SetArgPointee;
using tpm_manager::TpmManagerStatus;

namespace hwsec {

namespace {

constexpr std::string_view kFakeDelegateBlob = "fake_deleagte_blob";
constexpr std::string_view kFakeDelegateSecret = "fake_deleagte_secret";
constexpr std::string_view kFakeOwnerPassword = "fake_owner_password";

// TSS UUID matcher.
MATCHER_P(MatchTssUUID, uuid, "") {
  return arg.ulTimeLow == uuid.ulTimeLow && arg.usTimeMid == uuid.usTimeMid &&
         arg.usTimeHigh == uuid.usTimeHigh &&
         arg.bClockSeqHigh == uuid.bClockSeqHigh &&
         arg.bClockSeqLow == uuid.bClockSeqLow &&
         arg.rgbNode[0] == uuid.rgbNode[0] &&
         arg.rgbNode[1] == uuid.rgbNode[1] &&
         arg.rgbNode[2] == uuid.rgbNode[2] &&
         arg.rgbNode[3] == uuid.rgbNode[3] &&
         arg.rgbNode[4] == uuid.rgbNode[4] && arg.rgbNode[5] == uuid.rgbNode[5];
}

MATCHER_P(MemEq, expected, "") {
  return memcmp(std::data(expected), arg, std::size(expected)) == 0;
}

}  // namespace

BackendTpm1TestBase::BackendTpm1TestBase() = default;
BackendTpm1TestBase::~BackendTpm1TestBase() = default;

void BackendTpm1TestBase::SetUp() {
  proxy_ = std::make_unique<ProxyForTest>();

  auto backend = std::make_unique<BackendTpm1>(*proxy_, MiddlewareDerivative{});
  backend_ = backend.get();

  middleware_owner_ = std::make_unique<MiddlewareOwner>(
      std::move(backend), ThreadingMode::kCurrentThread);

  backend_->set_middleware_derivative_for_test(middleware_owner_->Derive());

  EXPECT_CALL(proxy_->GetMockOveralls(), Ospi_Context_Create(_))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(kDefaultContext), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_Context_Connect(kDefaultContext, nullptr))
      .WillRepeatedly(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_Context_GetTpmObject(kDefaultContext, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<1>(kDefaultTpm), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockOveralls(), Ospi_Context_Close(kDefaultContext))
      .WillRepeatedly(Return(TPM_SUCCESS));
}

void BackendTpm1TestBase::SetupSrk() {
  const uint32_t kFakeSrkAuthUsage = 0x9876123;
  const uint32_t kFakeSrkUsagePolicy = 0x1283789;
  TSS_UUID SRK_UUID = TSS_UUID_SRK;

  tpm_manager::GetTpmNonsensitiveStatusReply reply;
  reply.set_status(TpmManagerStatus::STATUS_SUCCESS);
  reply.set_is_owned(true);
  EXPECT_CALL(proxy_->GetMockTpmManagerProxy(),
              GetTpmNonsensitiveStatus(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(reply), Return(true)));

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_Context_LoadKeyByUUID(kDefaultContext, TSS_PS_TYPE_SYSTEM,
                                         MatchTssUUID(SRK_UUID), _))
      .WillRepeatedly(
          DoAll(SetArgPointee<3>(kDefaultSrkHandle), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_GetAttribUint32(kDefaultSrkHandle, TSS_TSPATTRIB_KEY_INFO,
                                   TSS_TSPATTRIB_KEYINFO_AUTHUSAGE, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<3>(kFakeSrkAuthUsage), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_GetPolicyObject(kDefaultSrkHandle, TSS_POLICY_USAGE, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<2>(kFakeSrkUsagePolicy), Return(TPM_SUCCESS)));

  EXPECT_CALL(
      proxy_->GetMockOveralls(),
      Ospi_Policy_SetSecret(kFakeSrkUsagePolicy, TSS_SECRET_MODE_PLAIN, _, _))
      .WillRepeatedly(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_Key_GetPubKey(kDefaultSrkHandle, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(kDefaultSrkPubkey.size()),
                            SetArgPointee<2>(kDefaultSrkPubkey.data()),
                            Return(TPM_SUCCESS)));
}

void BackendTpm1TestBase::SetupGetTpmStatus() {
  tpm_manager::GetTpmStatusReply reply;
  reply.set_status(TpmManagerStatus::STATUS_SUCCESS);
  *reply.mutable_local_data()->mutable_owner_password() = kFakeOwnerPassword;
  *reply.mutable_local_data()->mutable_owner_delegate()->mutable_blob() =
      kFakeDelegateBlob;
  *reply.mutable_local_data()->mutable_owner_delegate()->mutable_secret() =
      kFakeDelegateSecret;
  EXPECT_CALL(proxy_->GetMockTpmManagerProxy(), GetTpmStatus(_, _, _, _))
      .Times(AtMost(1))
      .WillOnce(DoAll(SetArgPointee<1>(reply), Return(true)))
      .RetiresOnSaturation();
}

void BackendTpm1TestBase::SetupDelegate() {
  TSS_HPOLICY kPolicy1 = 0x9909;

  SetupGetTpmStatus();

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_GetPolicyObject(kDefaultTpm, TSS_POLICY_USAGE, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(kPolicy1), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_Policy_SetSecret(kPolicy1, TSS_SECRET_MODE_PLAIN, _, _))
      .With(Args<3, 2>(ElementsAreArray(kFakeDelegateSecret)))
      .WillRepeatedly(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_SetAttribData(kPolicy1, TSS_TSPATTRIB_POLICY_DELEGATION_INFO,
                                 TSS_TSPATTRIB_POLDEL_OWNERBLOB, _, _))
      .With(Args<4, 3>(ElementsAreArray(kFakeDelegateBlob)))
      .WillRepeatedly(Return(TPM_SUCCESS));

  // Setup cleanup
  EXPECT_CALL(
      proxy_->GetMockOveralls(),
      Ospi_SetAttribUint32(kPolicy1, TSS_TSPATTRIB_POLICY_DELEGATION_INFO,
                           TSS_TSPATTRIB_POLDEL_TYPE, TSS_DELEGATIONTYPE_NONE))
      .WillRepeatedly(Return(TPM_SUCCESS));
  EXPECT_CALL(proxy_->GetMockOveralls(), Ospi_Policy_FlushSecret(kPolicy1))
      .WillRepeatedly(Return(TPM_SUCCESS));
}

void BackendTpm1TestBase::SetupOwner() {
  TSS_HPOLICY kPolicy1 = 0x9909;

  SetupGetTpmStatus();

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_GetPolicyObject(kDefaultTpm, TSS_POLICY_USAGE, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(kPolicy1), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_Policy_SetSecret(kPolicy1, TSS_SECRET_MODE_PLAIN, _, _))
      .With(Args<3, 2>(ElementsAreArray(kFakeOwnerPassword)))
      .WillRepeatedly(Return(TPM_SUCCESS));

  // Setup cleanup
  EXPECT_CALL(proxy_->GetMockOveralls(), Ospi_Policy_FlushSecret(kPolicy1))
      .WillRepeatedly(Return(TPM_SUCCESS));
}

void BackendTpm1TestBase::SetupHandleByEkReadability(bool readable) {
  TPM_DELEGATE_OWNER_BLOB fake_owner_blob = {};
  if (readable) {
    SetupDelegate();
    fake_owner_blob.pub.permissions.per1 = TPM_DELEGATE_OwnerReadInternalPub;
  } else {
    SetupOwner();
  }
  EXPECT_CALL(proxy_->GetMockOveralls(),
              Orspi_UnloadBlob_TPM_DELEGATE_OWNER_BLOB_s(_, _, _, _))
      .With(Args<1, 2>(ElementsAreArray(kFakeDelegateBlob)))
      .WillOnce(DoAll(SetArgPointee<0>(kFakeDelegateBlob.size()),
                      SetArgPointee<3>(fake_owner_blob), Return(TPM_SUCCESS)));
}

brillo::Blob BackendTpm1TestBase::SetupGetPublicKeyDer(
    const brillo::Blob& fake_pubkey) {
  static constexpr uint8_t kFakeModulus[] = {
      0x00, 0xb1, 0x51, 0x8b, 0x94, 0x6a, 0xa1, 0x66, 0x91, 0xc5, 0x5a, 0xe5,
      0x9a, 0x8e, 0x33, 0x61, 0x04, 0x72, 0xf4, 0x4c, 0x28, 0x01, 0x01, 0x68,
      0x49, 0x2b, 0xcb, 0xba, 0x91, 0x11, 0xb8, 0xb0, 0x3d, 0x13, 0xb9, 0xf2,
      0x48, 0x40, 0x03, 0xe5, 0x9e, 0x57, 0x6e, 0xc9, 0xa2, 0xee, 0x12, 0x02,
      0x81, 0xde, 0x47, 0xff, 0x2f, 0xfc, 0x18, 0x71, 0xcf, 0x1a, 0xf6, 0xa7,
      0x13, 0x7c, 0x7d, 0x30, 0x3f, 0x40, 0xa2, 0x05, 0xed, 0x7d, 0x3a, 0x2f,
      0xcc, 0xbd, 0xd3, 0xd9, 0x1a, 0x76, 0xd1, 0xec, 0xd5, 0x42, 0xdb, 0x1d,
      0x64, 0x5e, 0x66, 0x00, 0x04, 0x75, 0x49, 0xb7, 0x40, 0x4d, 0xae, 0x8f,
      0xbd, 0x8b, 0x81, 0x8a, 0x34, 0xd8, 0xb9, 0x4d, 0xd2, 0xfe, 0xc9, 0x08,
      0x16, 0x6c, 0x32, 0x77, 0x2b, 0xad, 0x21, 0xa5, 0xaa, 0x3f, 0x00, 0xcf,
      0x19, 0x0a, 0x4e, 0xc2, 0x9b, 0x01, 0xef, 0x60, 0x60, 0x88, 0x33, 0x1e,
      0x62, 0xd7, 0x22, 0x56, 0x7b, 0xb1, 0x26, 0xd1, 0xe4, 0x4f, 0x0c, 0xfc,
      0xfc, 0xe7, 0x1f, 0x56, 0xef, 0x6c, 0x6a, 0xa4, 0x2f, 0xa2, 0x62, 0x62,
      0x2a, 0x89, 0xd2, 0x5c, 0x3f, 0x96, 0xc9, 0x7c, 0x54, 0x5f, 0xd6, 0xe2,
      0xa1, 0xa0, 0x59, 0xef, 0x57, 0xc5, 0xb2, 0xa8, 0x80, 0x04, 0xde, 0x29,
      0x14, 0x19, 0x9a, 0x0d, 0x49, 0x09, 0xd7, 0xbb, 0x9c, 0xc9, 0x15, 0x7a,
      0x33, 0x8a, 0x35, 0x14, 0x01, 0x4a, 0x65, 0x39, 0x8c, 0x68, 0x73, 0x91,
      0x8c, 0x70, 0xa7, 0x10, 0x7a, 0x3e, 0xff, 0xd6, 0x1b, 0xa7, 0x29, 0xad,
      0x35, 0x12, 0xeb, 0x0c, 0x26, 0xd5, 0x36, 0xa5, 0xfb, 0xab, 0x42, 0x7b,
      0xeb, 0xc9, 0x45, 0x3c, 0x6d, 0x69, 0x32, 0x36, 0xd0, 0x43, 0xf3, 0xc3,
      0x2d, 0x0a, 0xcd, 0x31, 0xf0, 0xea, 0xf3, 0x44, 0xa2, 0x00, 0x83, 0xf5,
      0x93, 0x57, 0x49, 0xd8, 0xf5,
  };
  static constexpr uint8_t kFakeParms[] = {0xde, 0xad, 0xbe, 0xef, 0x12,
                                           0x34, 0x56, 0x78, 0x90};
  EXPECT_CALL(proxy_->GetMockOveralls(), Orspi_UnloadBlob_PUBKEY_s(_, _, _, _))
      .With(Args<1, 2>(ElementsAreArray(fake_pubkey)))
      .WillOnce([&](uint64_t* offset, auto&&, auto&&, TPM_PUBKEY* tpm_pubkey) {
        *offset = fake_pubkey.size();
        uint8_t* parms_ptr = static_cast<uint8_t*>(malloc(sizeof(kFakeParms)));
        memcpy(parms_ptr, kFakeParms, sizeof(kFakeParms));
        uint8_t* key_ptr = static_cast<uint8_t*>(malloc(sizeof(kFakeModulus)));
        memcpy(key_ptr, kFakeModulus, sizeof(kFakeModulus));
        *tpm_pubkey = TPM_PUBKEY{
            .algorithmParms =
                TPM_KEY_PARMS{
                    .algorithmID = TPM_ALG_RSA,
                    .encScheme = TPM_ES_NONE,
                    .sigScheme = TPM_SS_NONE,
                    .parmSize = sizeof(kFakeParms),
                    .parms = parms_ptr,
                },
            .pubKey =
                TPM_STORE_PUBKEY{
                    .keyLength = static_cast<uint32_t>(sizeof(kFakeModulus)),
                    .key = key_ptr,
                },
        };
        return TPM_SUCCESS;
      });
  EXPECT_CALL(proxy_->GetMockOveralls(),
              Orspi_UnloadBlob_RSA_KEY_PARMS_s(_, _, _, _))
      .With(Args<1, 2>(ElementsAreArray(kFakeParms)))
      .WillOnce(DoAll(SetArgPointee<0>(sizeof(kFakeParms)),
                      SetArgPointee<3>(TPM_RSA_KEY_PARMS{
                          .keyLength = 0,
                          .numPrimes = 0,
                          .exponentSize = 0,
                          .exponent = nullptr,
                      }),
                      Return(TPM_SUCCESS)));
  brillo::Blob modulus(kFakeModulus, kFakeModulus + sizeof(kFakeModulus));
  crypto::ScopedRSA fake_rsa = hwsec_foundation::CreateRSAFromNumber(
      modulus, hwsec_foundation::kWellKnownExponent);
  std::string fake_public_key_der =
      hwsec_foundation::RSAPublicKeyToString(fake_rsa);
  return BlobFromString(fake_public_key_der);
}

}  // namespace hwsec
