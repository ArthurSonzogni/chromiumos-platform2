// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM1_BACKEND_TEST_BASE_H_
#define LIBHWSEC_BACKEND_TPM1_BACKEND_TEST_BASE_H_

#include <memory>
#include <string>
#include <utility>

#include <absl/base/attributes.h>
#include <gtest/gtest.h>

#include "libhwsec/backend/tpm1/backend.h"
#include "libhwsec/error/tpm1_error.h"
#include "libhwsec/middleware/middleware.h"
#include "libhwsec/proxy/proxy_for_test.h"
#include "libhwsec/status.h"

namespace hwsec {

class BackendTpm1TestBase : public ::testing::Test {
 public:
  BackendTpm1TestBase() {}
  BackendTpm1TestBase(const BackendTpm1TestBase&) = delete;
  BackendTpm1TestBase& operator=(const BackendTpm1TestBase&) = delete;
  virtual ~BackendTpm1TestBase() {}

  void SetUp() override {
    proxy_ = std::make_unique<ProxyForTest>();

    auto backend =
        std::make_unique<BackendTpm1>(*proxy_, MiddlewareDerivative{});
    backend_ = backend.get();

    middleware_owner_ = std::make_unique<MiddlewareOwner>(
        std::move(backend),
        base::SequencedTaskRunnerHandle::IsSet()
            ? base::SequencedTaskRunnerHandle::Get()
            : nullptr,
        base::PlatformThread::CurrentId());

    backend_->set_middleware_derivative_for_test(middleware_owner_->Derive());

    middleware_ = std::make_unique<Middleware>(middleware_owner_->Derive());

    using testing::_;
    using testing::DoAll;
    using testing::Return;
    using testing::SetArgPointee;

    EXPECT_CALL(proxy_->GetMock().overalls, Ospi_Context_Create(_))
        .WillRepeatedly(
            DoAll(SetArgPointee<0>(kDefaultContext), Return(TPM_SUCCESS)));

    EXPECT_CALL(proxy_->GetMock().overalls,
                Ospi_Context_Connect(kDefaultContext, nullptr))
        .WillRepeatedly(Return(TPM_SUCCESS));

    EXPECT_CALL(proxy_->GetMock().overalls,
                Ospi_Context_GetTpmObject(kDefaultContext, _))
        .WillRepeatedly(
            DoAll(SetArgPointee<1>(kDefaultTpm), Return(TPM_SUCCESS)));

    EXPECT_CALL(proxy_->GetMock().overalls, Ospi_Context_Close(kDefaultContext))
        .WillRepeatedly(Return(TPM_SUCCESS));
  }

 protected:
  static inline constexpr TSS_HCONTEXT kDefaultContext = 9876;
  static inline constexpr TSS_HTPM kDefaultTpm = 6543;
  static inline constexpr TSS_HTPM kDefaultDelegateTpm = 9527;
  static inline constexpr uint32_t kDefaultSrkHandle = 5566123;

  void SetupSrk() {
    using testing::_;
    using testing::DoAll;
    using testing::Return;
    using testing::SetArgPointee;
    using tpm_manager::TpmManagerStatus;

    const uint32_t kFakeSrkAuthUsage = 0x9876123;
    const uint32_t kFakeSrkUsagePolicy = 0x1283789;

    tpm_manager::GetTpmNonsensitiveStatusReply reply;
    reply.set_status(TpmManagerStatus::STATUS_SUCCESS);
    reply.set_is_owned(true);
    EXPECT_CALL(proxy_->GetMock().tpm_manager,
                GetTpmNonsensitiveStatus(_, _, _, _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(reply), Return(true)));

    EXPECT_CALL(
        proxy_->GetMock().overalls,
        Ospi_Context_LoadKeyByUUID(kDefaultContext, TSS_PS_TYPE_SYSTEM, _, _))
        .WillOnce(
            DoAll(SetArgPointee<3>(kDefaultSrkHandle), Return(TPM_SUCCESS)));

    EXPECT_CALL(proxy_->GetMock().overalls,
                Ospi_GetAttribUint32(kDefaultSrkHandle, TSS_TSPATTRIB_KEY_INFO,
                                     TSS_TSPATTRIB_KEYINFO_AUTHUSAGE, _))
        .WillOnce(
            DoAll(SetArgPointee<3>(kFakeSrkAuthUsage), Return(TPM_SUCCESS)));

    EXPECT_CALL(proxy_->GetMock().overalls,
                Ospi_GetPolicyObject(kDefaultSrkHandle, TSS_POLICY_USAGE, _))
        .WillOnce(
            DoAll(SetArgPointee<2>(kFakeSrkUsagePolicy), Return(TPM_SUCCESS)));

    EXPECT_CALL(
        proxy_->GetMock().overalls,
        Ospi_Policy_SetSecret(kFakeSrkUsagePolicy, TSS_SECRET_MODE_PLAIN, _, _))
        .WillOnce(Return(TPM_SUCCESS));

    EXPECT_CALL(proxy_->GetMock().overalls,
                Ospi_Key_GetPubKey(kDefaultSrkHandle, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(kDefaultSrkPubkey.size()),
                        SetArgPointee<2>(kDefaultSrkPubkey.data()),
                        Return(TPM_SUCCESS)));
  }

  void SetupDelegate() {
    using testing::_;
    using testing::Args;
    using testing::AtMost;
    using testing::DoAll;
    using testing::ElementsAreArray;
    using testing::Return;
    using testing::SetArgPointee;
    using tpm_manager::TpmManagerStatus;

    // Cache the default user TPM handle.
    auto user_tpm = backend_->GetUserTpmHandle();
    ASSERT_TRUE(user_tpm.ok());
    EXPECT_EQ(*user_tpm, kDefaultTpm);

    TSS_HPOLICY kPolicy1 = 0x9909;

    std::string fake_delegate_blob = "fake_deleagte_blob";
    std::string fake_delegate_secret = "fake_deleagte_secret";

    tpm_manager::GetTpmStatusReply reply;
    reply.set_status(TpmManagerStatus::STATUS_SUCCESS);
    *reply.mutable_local_data()->mutable_owner_delegate()->mutable_blob() =
        fake_delegate_blob;
    *reply.mutable_local_data()->mutable_owner_delegate()->mutable_secret() =
        fake_delegate_secret;
    EXPECT_CALL(proxy_->GetMock().tpm_manager, GetTpmStatus(_, _, _, _))
        .Times(AtMost(1))
        .WillOnce(DoAll(SetArgPointee<1>(reply), Return(true)))
        .RetiresOnSaturation();

    EXPECT_CALL(proxy_->GetMock().overalls,
                Ospi_Context_GetTpmObject(kDefaultContext, _))
        .Times(AtMost(1))
        .WillOnce(
            DoAll(SetArgPointee<1>(kDefaultDelegateTpm), Return(TPM_SUCCESS)))
        .RetiresOnSaturation();

    EXPECT_CALL(proxy_->GetMock().overalls,
                Ospi_GetPolicyObject(kDefaultDelegateTpm, TSS_POLICY_USAGE, _))
        .WillRepeatedly(DoAll(SetArgPointee<2>(kPolicy1), Return(TPM_SUCCESS)));

    EXPECT_CALL(proxy_->GetMock().overalls,
                Ospi_Policy_SetSecret(kPolicy1, TSS_SECRET_MODE_PLAIN, _, _))
        .With(Args<3, 2>(ElementsAreArray(fake_delegate_secret)))
        .WillRepeatedly(Return(TPM_SUCCESS));

    EXPECT_CALL(
        proxy_->GetMock().overalls,
        Ospi_SetAttribData(kPolicy1, TSS_TSPATTRIB_POLICY_DELEGATION_INFO,
                           TSS_TSPATTRIB_POLDEL_OWNERBLOB, _, _))
        .With(Args<4, 3>(ElementsAreArray(fake_delegate_blob)))
        .WillRepeatedly(Return(TPM_SUCCESS));
  }

  brillo::Blob kDefaultSrkPubkey = brillo::BlobFromString("default_srk");
  std::unique_ptr<ProxyForTest> proxy_;
  std::unique_ptr<MiddlewareOwner> middleware_owner_;
  std::unique_ptr<Middleware> middleware_;
  BackendTpm1* backend_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM1_BACKEND_TEST_BASE_H_
