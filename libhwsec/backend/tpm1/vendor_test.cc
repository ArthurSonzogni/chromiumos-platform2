// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "libhwsec/backend/tpm1/backend_test_base.h"

using hwsec_foundation::error::testing::ReturnError;
using hwsec_foundation::error::testing::ReturnValue;
using testing::_;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;
using testing::SetArgPointee;
using tpm_manager::TpmManagerStatus;
namespace hwsec {

class BackendVendorTpm1Test : public BackendTpm1TestBase {};

TEST_F(BackendVendorTpm1Test, GetVersionInfo) {
  const brillo::Blob kFakeVendorSpecific = {0x06, 0x2B, 0x00, 0xF3, 0x00,
                                            0x74, 0x70, 0x6D, 0x73, 0x31,
                                            0x35, 0xFF, 0xFF};
  tpm_manager::GetVersionInfoReply reply;
  reply.set_status(TpmManagerStatus::STATUS_SUCCESS);
  reply.set_family(0x312E3200);
  reply.set_spec_level(0x200000003);
  reply.set_manufacturer(0x49465800);
  reply.set_tpm_model(0xFFFFFFFF);
  reply.set_firmware_version(0x62B);
  reply.set_vendor_specific(brillo::BlobToString(kFakeVendorSpecific));
  reply.set_gsc_version(tpm_manager::GSC_VERSION_NOT_GSC);
  EXPECT_CALL(proxy_->GetMock().tpm_manager, GetVersionInfo(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(reply), Return(true)));

  auto family = middleware_->CallSync<&Backend::Vendor::GetFamily>();
  ASSERT_TRUE(family.ok());
  EXPECT_EQ(*family, 0x312E3200);

  auto spec_level = middleware_->CallSync<&Backend::Vendor::GetSpecLevel>();
  ASSERT_TRUE(spec_level.ok());
  EXPECT_EQ(*spec_level, 0x200000003);

  auto manufacturer =
      middleware_->CallSync<&Backend::Vendor::GetManufacturer>();
  ASSERT_TRUE(manufacturer.ok());
  EXPECT_EQ(*manufacturer, 0x49465800);

  auto model = middleware_->CallSync<&Backend::Vendor::GetTpmModel>();
  ASSERT_TRUE(model.ok());
  EXPECT_EQ(*model, 0xFFFFFFFF);

  auto fw_ver = middleware_->CallSync<&Backend::Vendor::GetFirmwareVersion>();
  ASSERT_TRUE(fw_ver.ok());
  EXPECT_EQ(*fw_ver, 0x62B);

  auto vendor_specific =
      middleware_->CallSync<&Backend::Vendor::GetVendorSpecific>();
  ASSERT_TRUE(vendor_specific.ok());
  EXPECT_EQ(*vendor_specific, kFakeVendorSpecific);

  auto fingerprint = middleware_->CallSync<&Backend::Vendor::GetFingerprint>();
  ASSERT_TRUE(fingerprint.ok());
  EXPECT_EQ(*fingerprint, 0x2081EE27);
}

}  // namespace hwsec
