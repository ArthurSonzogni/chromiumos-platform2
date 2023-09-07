// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client-test/tpm_manager/dbus-proxy-mocks.h>

#include "libhwsec/backend/tpm1/backend_test_base.h"
#include "libhwsec/overalls/mock_overalls.h"

using hwsec_foundation::error::testing::IsOk;
using hwsec_foundation::error::testing::IsOkAndHolds;
using hwsec_foundation::error::testing::ReturnError;
using hwsec_foundation::error::testing::ReturnValue;
using testing::_;
using testing::Args;
using testing::DoAll;
using testing::ElementsAreArray;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;
using testing::SetArgPointee;

namespace hwsec {

using TssHelperTest = BackendTpm1TestBase;

TEST_F(TssHelperTest, GetScopedTssContext) {
  TSS_HCONTEXT kFakeContext = 0x5566;

  EXPECT_CALL(proxy_->GetMockOveralls(), Ospi_Context_Create(_))
      .WillOnce(DoAll(SetArgPointee<0>(kFakeContext), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_Context_Connect(kFakeContext, nullptr))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMockOveralls(), Ospi_Context_Close(kFakeContext))
      .WillOnce(Return(TPM_SUCCESS));

  auto result = backend_->GetTssHelper().GetScopedTssContext();
  ASSERT_OK(result);
  EXPECT_EQ(result->value(), kFakeContext);
}

TEST_F(TssHelperTest, GetTssContext) {
  TSS_HCONTEXT kFakeContext = 0x1234;

  EXPECT_CALL(proxy_->GetMockOveralls(), Ospi_Context_Create(_))
      .WillOnce(DoAll(SetArgPointee<0>(kFakeContext), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_Context_Connect(kFakeContext, nullptr))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMockOveralls(), Ospi_Context_Close(kFakeContext))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_THAT(backend_->GetTssHelper().GetTssContext(),
              IsOkAndHolds(kFakeContext));

  // Run again to check the cache works correctly.
  EXPECT_THAT(backend_->GetTssHelper().GetTssContext(),
              IsOkAndHolds(kFakeContext));
}

TEST_F(TssHelperTest, GetTpmHandle) {
  TSS_HCONTEXT kFakeContext = 0x1234;
  TSS_HTPM kFakeTpm = 0x5678;

  EXPECT_CALL(proxy_->GetMockOveralls(), Ospi_Context_Create(_))
      .WillOnce(DoAll(SetArgPointee<0>(kFakeContext), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_Context_Connect(kFakeContext, nullptr))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_Context_GetTpmObject(kFakeContext, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFakeTpm), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockOveralls(), Ospi_Context_Close(kFakeContext))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_THAT(backend_->GetTssHelper().GetTpmHandle(), IsOkAndHolds(kFakeTpm));

  // Run again to check the cache works correctly.
  EXPECT_THAT(backend_->GetTssHelper().GetTpmHandle(), IsOkAndHolds(kFakeTpm));
}

TEST_F(TssHelperTest, GetDelegateTpmHandle) {
  TSS_HCONTEXT kFakeContext = 0x1234;
  TSS_HTPM kFakeTpm = 0x5678;
  TSS_HPOLICY kPolicy = 0x9901;

  std::string fake_delegate_blob = "fake_deleagte_blob";
  std::string fake_delegate_secret = "fake_deleagte_secret";

  tpm_manager::GetTpmStatusReply reply;
  *reply.mutable_local_data()->mutable_owner_delegate()->mutable_blob() =
      fake_delegate_blob;
  *reply.mutable_local_data()->mutable_owner_delegate()->mutable_secret() =
      fake_delegate_secret;
  EXPECT_CALL(proxy_->GetMockTpmManagerProxy(), GetTpmStatus(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(reply), Return(true)));

  EXPECT_CALL(proxy_->GetMockOveralls(), Ospi_Context_Create(_))
      .WillOnce(DoAll(SetArgPointee<0>(kFakeContext), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_Context_Connect(kFakeContext, nullptr))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_Context_GetTpmObject(kFakeContext, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFakeTpm), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_GetPolicyObject(kFakeTpm, TSS_POLICY_USAGE, _))
      .WillOnce(DoAll(SetArgPointee<2>(kPolicy), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_Policy_SetSecret(kPolicy, TSS_SECRET_MODE_PLAIN, _, _))
      .With(Args<3, 2>(ElementsAreArray(fake_delegate_secret)))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_SetAttribData(kPolicy, TSS_TSPATTRIB_POLICY_DELEGATION_INFO,
                                 TSS_TSPATTRIB_POLDEL_OWNERBLOB, _, _))
      .With(Args<4, 3>(ElementsAreArray(fake_delegate_blob)))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_Context_CloseObject(kFakeContext, kFakeTpm))
      .WillOnce(Return(TPM_SUCCESS));
  EXPECT_CALL(proxy_->GetMockOveralls(), Ospi_Context_Close(kFakeContext))
      .WillOnce(Return(TPM_SUCCESS));

  // Setup DelegateHandleSettingCleanup
  EXPECT_CALL(
      proxy_->GetMockOveralls(),
      Ospi_SetAttribUint32(kPolicy, TSS_TSPATTRIB_POLICY_DELEGATION_INFO,
                           TSS_TSPATTRIB_POLDEL_TYPE, TSS_DELEGATIONTYPE_NONE))
      .WillOnce(Return(TPM_SUCCESS));
  EXPECT_CALL(proxy_->GetMockOveralls(), Ospi_Policy_FlushSecret(kPolicy))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_THAT(backend_->GetTssHelper().GetTpmHandle(), IsOkAndHolds(kFakeTpm));
  EXPECT_THAT(backend_->GetTssHelper().SetTpmHandleAsDelegate(), IsOk());
}

TEST_F(TssHelperTest, GetOwnerTpmHandle) {
  TSS_HCONTEXT kFakeContext = 0x1234;
  TSS_HTPM kFakeTpm = 0x5678;
  TSS_HPOLICY kPolicy = 0x9901;

  std::string fake_owner_password = "fake_owner_password";

  tpm_manager::GetTpmStatusReply reply;
  *reply.mutable_local_data()->mutable_owner_password() = fake_owner_password;
  EXPECT_CALL(proxy_->GetMockTpmManagerProxy(), GetTpmStatus(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(reply), Return(true)));

  EXPECT_CALL(proxy_->GetMockOveralls(), Ospi_Context_Create(_))
      .WillOnce(DoAll(SetArgPointee<0>(kFakeContext), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_Context_Connect(kFakeContext, nullptr))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_Context_GetTpmObject(kFakeContext, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFakeTpm), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_GetPolicyObject(kFakeTpm, TSS_POLICY_USAGE, _))
      .WillOnce(DoAll(SetArgPointee<2>(kPolicy), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_Policy_SetSecret(kPolicy, TSS_SECRET_MODE_PLAIN, _, _))
      .With(Args<3, 2>(ElementsAreArray(fake_owner_password)))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_Context_CloseObject(kFakeContext, kFakeTpm))
      .WillOnce(Return(TPM_SUCCESS));
  EXPECT_CALL(proxy_->GetMockOveralls(), Ospi_Context_Close(kFakeContext))
      .WillOnce(Return(TPM_SUCCESS));

  // Setup OwnerHandleSettingCleanup
  EXPECT_CALL(proxy_->GetMockOveralls(), Ospi_Policy_FlushSecret(kPolicy))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_THAT(backend_->GetTssHelper().GetTpmHandle(), IsOkAndHolds(kFakeTpm));
  EXPECT_THAT(backend_->GetTssHelper().SetTpmHandleAsOwner(), IsOk());
}

}  // namespace hwsec
