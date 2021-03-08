// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tpm_manager/server/tpm_nvram_impl.h"

#include <libhwsec/test_utils/tpm1/test_fixture.h>

#include "tpm_manager/common/typedefs.h"
#include "tpm_manager/server/mock_local_data_store.h"
#include "tpm_manager/server/mock_openssl_crypto_util.h"
#include "tpm_manager/server/mock_tpm_status.h"

namespace tpm_manager {

namespace {

using testing::_;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;
using testing::SetArrayArgument;

constexpr TSS_HCONTEXT kFakeContext = 99999;
constexpr TSS_HTPM kFakeTpm = 66666;

MATCHER_P(UstrEq, str, "") {
  std::string arg_str(reinterpret_cast<char*>(arg), str.length());
  return arg_str == str;
}

}  // namespace

class TpmNvramTest : public ::hwsec::Tpm1HwsecTest {
 public:
  TpmNvramTest()
      : tpm_nvram_(&mock_data_store_),
        fake_local_data_(mock_data_store_.GetMutableFakeData()) {
    ON_CALL_OVERALLS(Ospi_Context_Create(_))
        .WillByDefault(
            DoAll(SetArgPointee<0>(kFakeContext), Return(TSS_SUCCESS)));
    ON_CALL_OVERALLS(Ospi_Context_GetTpmObject(_, _))
        .WillByDefault(DoAll(SetArgPointee<1>(kFakeTpm), Return(TSS_SUCCESS)));
  }
  ~TpmNvramTest() override = default;

 protected:
  NiceMock<MockLocalDataStore> mock_data_store_;
  TpmNvramImpl tpm_nvram_;
  // Holds a reference of the internal |LocalData| of |mock_data_store_|.
  LocalData& fake_local_data_;
};

TEST_F(TpmNvramTest, DefineSpaceSuccess) {
  std::string owner_password = "owner";
  fake_local_data_.set_owner_password(owner_password);
  constexpr uint32_t kIndex = 0x87;
  constexpr size_t kSize = 0x20;
  const std::vector<NvramSpaceAttribute> attributes;
  const std::string authorization_value;
  NvramSpacePolicy policy = NVRAM_POLICY_NONE;
  constexpr TSS_HNVSTORE kNvKandle = 52;

  EXPECT_CALL_OVERALLS(
      Ospi_Context_CreateObject(kFakeContext, TSS_OBJECT_TYPE_NV, 0, _))
      .WillOnce(DoAll(SetArgPointee<3>(kNvKandle), Return(TSS_SUCCESS)));
  EXPECT_CALL_OVERALLS(
      Ospi_SetAttribUint32(kNvKandle, TSS_TSPATTRIB_NV_INDEX, 0, kIndex))
      .WillOnce(Return(TSS_SUCCESS));
  EXPECT_CALL_OVERALLS(
      Ospi_SetAttribUint32(kNvKandle, TSS_TSPATTRIB_NV_DATASIZE, 0, kSize))
      .WillOnce(Return(TSS_SUCCESS));
  EXPECT_CALL_OVERALLS(
      Ospi_SetAttribUint32(kNvKandle, TSS_TSPATTRIB_NV_PERMISSIONS, 0, 0))
      .WillOnce(Return(TSS_SUCCESS));
  EXPECT_CALL_OVERALLS(Ospi_NV_DefineSpace(kNvKandle, 0, 0))
      .WillOnce(Return(TSS_SUCCESS));

  EXPECT_EQ(tpm_nvram_.DefineSpace(kIndex, kSize, attributes,
                                   authorization_value, policy),
            NVRAM_RESULT_SUCCESS);
}

TEST_F(TpmNvramTest, DefineSpaceFail) {
  std::string owner_password = "owner";
  fake_local_data_.set_owner_password(owner_password);
  constexpr uint32_t kIndex = 0x87;
  constexpr size_t kSize = 0x20;
  const std::vector<NvramSpaceAttribute> attributes;
  const std::string authorization_value;
  NvramSpacePolicy policy = NVRAM_POLICY_NONE;

  EXPECT_CALL_OVERALLS(
      Ospi_SetAttribUint32(_, TSS_TSPATTRIB_NV_INDEX, 0, kIndex))
      .WillOnce(Return(TSS_SUCCESS));
  EXPECT_CALL_OVERALLS(
      Ospi_SetAttribUint32(_, TSS_TSPATTRIB_NV_DATASIZE, 0, kSize))
      .WillOnce(Return(TSS_SUCCESS));
  EXPECT_CALL_OVERALLS(
      Ospi_SetAttribUint32(_, TSS_TSPATTRIB_NV_PERMISSIONS, 0, 0))
      .WillOnce(Return(TSS_SUCCESS));
  EXPECT_CALL_OVERALLS(Ospi_NV_DefineSpace(_, _, _))
      .WillOnce(Return(TPM_E_AUTHFAIL));

  EXPECT_EQ(tpm_nvram_.DefineSpace(kIndex, kSize, attributes,
                                   authorization_value, policy),
            NVRAM_RESULT_ACCESS_DENIED);
}

TEST_F(TpmNvramTest, DefineSpaceNoOwnerPassword) {
  constexpr uint32_t kIndex = 0x87;
  constexpr size_t kSize = 0x20;
  const std::vector<NvramSpaceAttribute> attributes;
  const std::string authorization_value;
  NvramSpacePolicy policy = NVRAM_POLICY_NONE;

  EXPECT_EQ(tpm_nvram_.DefineSpace(kIndex, kSize, attributes,
                                   authorization_value, policy),
            NVRAM_RESULT_OPERATION_DISABLED);
}

TEST_F(TpmNvramTest, DefineSpaceSetPCR0) {
  std::string owner_password = "owner";
  fake_local_data_.set_owner_password(owner_password);
  constexpr uint32_t kIndex = 0x87;
  constexpr size_t kSize = 0x20;
  const std::vector<NvramSpaceAttribute> attributes;
  const std::string authorization_value;
  NvramSpacePolicy policy = NVRAM_POLICY_PCR0;

  constexpr unsigned int kTpmBootPCR = 0;
  constexpr unsigned int kTpmPCRLocality = 1;
  constexpr int kPcrLen = 32;
  constexpr char kFakePcr0[] = "01234567890123456789012345678901";
  constexpr TSS_HNVSTORE kNvKandle = 1725;
  constexpr TSS_HPCRS kPcrKandle = 9527;

  EXPECT_CALL_OVERALLS(
      Ospi_Context_CreateObject(kFakeContext, TSS_OBJECT_TYPE_NV, 0, _))
      .WillOnce(DoAll(SetArgPointee<3>(kNvKandle), Return(TSS_SUCCESS)));
  EXPECT_CALL_OVERALLS(Ospi_Context_CreateObject(kFakeContext,
                                                 TSS_OBJECT_TYPE_PCRS,
                                                 TSS_PCRS_STRUCT_INFO_SHORT, _))
      .WillOnce(DoAll(SetArgPointee<3>(kPcrKandle), Return(TSS_SUCCESS)));

  // ScopedTssMemory should free this pcr0.
  uint8_t* pcr0 = new uint8_t[kPcrLen];
  memcpy(pcr0, kFakePcr0, kPcrLen);

  EXPECT_CALL_OVERALLS(Ospi_TPM_PcrRead(kFakeTpm, kTpmBootPCR, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kPcrLen), SetArgPointee<3>(pcr0),
                      Return(TSS_SUCCESS)));
  EXPECT_CALL_OVERALLS(
      Ospi_PcrComposite_SetPcrValue(kPcrKandle, kTpmBootPCR, kPcrLen, pcr0))
      .WillOnce(Return(TSS_SUCCESS));
  EXPECT_CALL_OVERALLS(
      Ospi_PcrComposite_SetPcrLocality(kPcrKandle, kTpmPCRLocality))
      .WillOnce(Return(TSS_SUCCESS));
  EXPECT_CALL_OVERALLS(
      Ospi_SetAttribUint32(kNvKandle, TSS_TSPATTRIB_NV_INDEX, 0, kIndex))
      .WillOnce(Return(TSS_SUCCESS));
  EXPECT_CALL_OVERALLS(
      Ospi_SetAttribUint32(kNvKandle, TSS_TSPATTRIB_NV_DATASIZE, 0, kSize))
      .WillOnce(Return(TSS_SUCCESS));
  EXPECT_CALL_OVERALLS(
      Ospi_SetAttribUint32(kNvKandle, TSS_TSPATTRIB_NV_PERMISSIONS, 0, 0))
      .WillOnce(Return(TSS_SUCCESS));
  EXPECT_CALL_OVERALLS(Ospi_NV_DefineSpace(kNvKandle, kPcrKandle, kPcrKandle))
      .WillOnce(Return(TSS_SUCCESS));

  EXPECT_EQ(tpm_nvram_.DefineSpace(kIndex, kSize, attributes,
                                   authorization_value, policy),
            NVRAM_RESULT_SUCCESS);
}

TEST_F(TpmNvramTest, DefineSpaceAttributes) {
  std::string owner_password = "owner";
  fake_local_data_.set_owner_password(owner_password);
  constexpr uint32_t kIndex = 0x23;
  constexpr size_t kSize = 0x30;
  const std::vector<NvramSpaceAttribute> attributes{
      NVRAM_PERSISTENT_WRITE_LOCK, NVRAM_BOOT_WRITE_LOCK, NVRAM_OWNER_WRITE};
  const std::string authorization_value;
  NvramSpacePolicy policy = NVRAM_POLICY_NONE;

  constexpr TSS_HNVSTORE kNvKandle = 5491;
  EXPECT_CALL_OVERALLS(
      Ospi_Context_CreateObject(kFakeContext, TSS_OBJECT_TYPE_NV, 0, _))
      .WillOnce(DoAll(SetArgPointee<3>(kNvKandle), Return(TSS_SUCCESS)));

  EXPECT_CALL_OVERALLS(
      Ospi_SetAttribUint32(kNvKandle, TSS_TSPATTRIB_NV_INDEX, 0, kIndex))
      .WillOnce(Return(TSS_SUCCESS));
  EXPECT_CALL_OVERALLS(
      Ospi_SetAttribUint32(kNvKandle, TSS_TSPATTRIB_NV_DATASIZE, 0, kSize))
      .WillOnce(Return(TSS_SUCCESS));
  EXPECT_CALL_OVERALLS(
      Ospi_SetAttribUint32(kNvKandle, TSS_TSPATTRIB_NV_PERMISSIONS, 0,
                           TPM_NV_PER_WRITEDEFINE | TPM_NV_PER_WRITE_STCLEAR |
                               TPM_NV_PER_OWNERWRITE))
      .WillOnce(Return(TSS_SUCCESS));
  EXPECT_CALL_OVERALLS(Ospi_NV_DefineSpace(kNvKandle, 0, 0))
      .WillOnce(Return(TSS_SUCCESS));

  EXPECT_EQ(tpm_nvram_.DefineSpace(kIndex, kSize, attributes,
                                   authorization_value, policy),
            NVRAM_RESULT_SUCCESS);
}

TEST_F(TpmNvramTest, DefineSpaceAuthAttributes) {
  std::string owner_password = "owner";
  fake_local_data_.set_owner_password(owner_password);
  constexpr uint32_t kIndex = 0x92;
  constexpr size_t kSize = 16;
  const std::vector<NvramSpaceAttribute> attributes{NVRAM_READ_AUTHORIZATION};
  const std::string authorization_value = "NF@ONsafsfF)A@N";
  NvramSpacePolicy policy = NVRAM_POLICY_NONE;

  constexpr TSS_HPOLICY kTpmUsagePolicy = 9321;
  EXPECT_CALL_OVERALLS(Ospi_GetPolicyObject(kFakeTpm, TSS_POLICY_USAGE, _))
      .WillOnce(DoAll(SetArgPointee<2>(kTpmUsagePolicy), Return(TSS_SUCCESS)));
  EXPECT_CALL_OVERALLS(
      Ospi_Policy_SetSecret(kTpmUsagePolicy, TSS_SECRET_MODE_PLAIN,
                            owner_password.size(), UstrEq(owner_password)))
      .WillOnce(Return(TSS_SUCCESS));

  constexpr TSS_HNVSTORE kNvKandle = 12345;
  EXPECT_CALL_OVERALLS(
      Ospi_Context_CreateObject(kFakeContext, TSS_OBJECT_TYPE_NV, 0, _))
      .WillOnce(DoAll(SetArgPointee<3>(kNvKandle), Return(TSS_SUCCESS)));

  constexpr TSS_HPOLICY kPolicyKandle = 54321;
  EXPECT_CALL_OVERALLS(Ospi_Context_CreateObject(kFakeContext,
                                                 TSS_OBJECT_TYPE_POLICY,
                                                 TSS_POLICY_USAGE, _))
      .WillOnce(DoAll(SetArgPointee<3>(kPolicyKandle), Return(TSS_SUCCESS)));

  EXPECT_CALL_OVERALLS(Ospi_Policy_SetSecret(kPolicyKandle,
                                             TSS_SECRET_MODE_PLAIN,
                                             authorization_value.size(),
                                             UstrEq(authorization_value)))
      .WillOnce(Return(TSS_SUCCESS));
  EXPECT_CALL_OVERALLS(Ospi_Policy_AssignToObject(kPolicyKandle, kNvKandle))
      .WillOnce(Return(TSS_SUCCESS));

  EXPECT_CALL_OVERALLS(
      Ospi_SetAttribUint32(kNvKandle, TSS_TSPATTRIB_NV_INDEX, 0, kIndex))
      .WillOnce(Return(TSS_SUCCESS));
  EXPECT_CALL_OVERALLS(
      Ospi_SetAttribUint32(kNvKandle, TSS_TSPATTRIB_NV_DATASIZE, 0, kSize))
      .WillOnce(Return(TSS_SUCCESS));
  EXPECT_CALL_OVERALLS(Ospi_SetAttribUint32(kNvKandle,
                                            TSS_TSPATTRIB_NV_PERMISSIONS, 0,
                                            TPM_NV_PER_AUTHREAD))
      .WillOnce(Return(TSS_SUCCESS));
  EXPECT_CALL_OVERALLS(Ospi_NV_DefineSpace(kNvKandle, _, _))
      .WillOnce(Return(TSS_SUCCESS));

  EXPECT_EQ(tpm_nvram_.DefineSpace(kIndex, kSize, attributes,
                                   authorization_value, policy),
            NVRAM_RESULT_SUCCESS);
}

}  // namespace tpm_manager
