// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "libhwsec/backend/tpm2/backend_test_base.h"

using hwsec_foundation::error::testing::IsOk;
using hwsec_foundation::error::testing::IsOkAndHolds;
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

class BackendVendorTpm2Test : public BackendTpm2TestBase {};

TEST_F(BackendVendorTpm2Test, GetVersionInfo) {
  const brillo::Blob kFakeVendorSpecific = {0x78, 0x43, 0x47, 0x20,
                                            0x66, 0x54, 0x50, 0x4D};
  tpm_manager::GetVersionInfoReply reply;
  reply.set_status(TpmManagerStatus::STATUS_SUCCESS);
  reply.set_family(0x322E3000);
  reply.set_spec_level(0x74);
  reply.set_manufacturer(0x43524F53);
  reply.set_tpm_model(1);
  reply.set_firmware_version(0x8E0F7DC508B56D7C);
  reply.set_vendor_specific(brillo::BlobToString(kFakeVendorSpecific));
  reply.set_gsc_version(tpm_manager::GSC_VERSION_CR50);
  EXPECT_CALL(proxy_->GetMock().tpm_manager, GetVersionInfo(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(reply), Return(true)));

  EXPECT_THAT(middleware_->CallSync<&Backend::Vendor::GetFamily>(),
              IsOkAndHolds(0x322E3000));

  EXPECT_THAT(middleware_->CallSync<&Backend::Vendor::GetSpecLevel>(),
              IsOkAndHolds(0x74));

  EXPECT_THAT(middleware_->CallSync<&Backend::Vendor::GetManufacturer>(),
              IsOkAndHolds(0x43524F53));

  EXPECT_THAT(middleware_->CallSync<&Backend::Vendor::GetTpmModel>(),
              IsOkAndHolds(1));

  EXPECT_THAT(middleware_->CallSync<&Backend::Vendor::GetFirmwareVersion>(),
              IsOkAndHolds(0x8E0F7DC508B56D7C));

  EXPECT_THAT(middleware_->CallSync<&Backend::Vendor::GetVendorSpecific>(),
              IsOkAndHolds(kFakeVendorSpecific));

  EXPECT_THAT(middleware_->CallSync<&Backend::Vendor::GetFingerprint>(),
              IsOkAndHolds(0x2A0797FD));
}

TEST_F(BackendVendorTpm2Test, IsSrkRocaVulnerable) {
  EXPECT_THAT(middleware_->CallSync<&Backend::Vendor::IsSrkRocaVulnerable>(),
              IsOkAndHolds(false));
}

TEST_F(BackendVendorTpm2Test, DeclareTpmFirmwareStable) {
  EXPECT_CALL(proxy_->GetMock().tpm_utility, DeclareTpmFirmwareStable())
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));

  EXPECT_THAT(
      middleware_->CallSync<&Backend::Vendor::DeclareTpmFirmwareStable>(),
      IsOk());

  EXPECT_THAT(
      middleware_->CallSync<&Backend::Vendor::DeclareTpmFirmwareStable>(),
      IsOk());
}

TEST_F(BackendVendorTpm2Test, SendRawCommand) {
  const brillo::Blob kFakeRequest = {0x80, 0x01, 0x00, 0x00, 0x00, 0x14, 0xba,
                                     0xcc, 0xd0, 0x0a, 0x00, 0x04, 0x00, 0x00,
                                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  const std::string kFakeInput = brillo::BlobToString(kFakeRequest);
  const brillo::Blob kFakeResponse = {
      0x80, 0x01, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x04, 0x40, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6f, 0x00, 0x00, 0x00, 0x06,
      0x00, 0x00, 0x00, 0x00, 0xaa, 0x66, 0x15, 0x0f, 0x87, 0xb7, 0x3b, 0x67};
  const std::string kFakeOutput = brillo::BlobToString(kFakeResponse);

  EXPECT_CALL(proxy_->GetMock().trunks_command_transceiver,
              SendCommandAndWait(kFakeInput))
      .WillOnce(Return(kFakeOutput));

  EXPECT_THAT(
      middleware_->CallSync<&Backend::Vendor::SendRawCommand>(kFakeRequest),
      IsOkAndHolds(kFakeResponse));
}

TEST_F(BackendVendorTpm2Test, GetRsuDeviceId) {
  const std::string kFakeRsuDeviceId = "fake_rsu_device_id";

  EXPECT_CALL(proxy_->GetMock().tpm_utility, GetRsuDeviceId(_))
      .WillOnce(DoAll(SetArgPointee<0>(kFakeRsuDeviceId),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_THAT(middleware_->CallSync<&Backend::Vendor::GetRsuDeviceId>(),
              IsOkAndHolds(brillo::BlobFromString(kFakeRsuDeviceId)));
}

}  // namespace hwsec
