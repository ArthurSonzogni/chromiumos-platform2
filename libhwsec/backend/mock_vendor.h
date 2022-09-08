// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_MOCK_VENDOR_H_
#define LIBHWSEC_BACKEND_MOCK_VENDOR_H_

#include <cstdint>

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

#include "libhwsec/backend/vendor.h"
#include "libhwsec/status.h"

namespace hwsec {

class MockVendor : public Vendor {
 public:
  MOCK_METHOD(StatusOr<uint32_t>, GetFamily, (), (override));
  MOCK_METHOD(StatusOr<uint64_t>, GetSpecLevel, (), (override));
  MOCK_METHOD(StatusOr<uint32_t>, GetManufacturer, (), (override));
  MOCK_METHOD(StatusOr<uint32_t>, GetTpmModel, (), (override));
  MOCK_METHOD(StatusOr<uint64_t>, GetFirmwareVersion, (), (override));
  MOCK_METHOD(StatusOr<brillo::Blob>, GetVendorSpecific, (), (override));
  MOCK_METHOD(StatusOr<int32_t>, GetFingerprint, (), (override));
  MOCK_METHOD(StatusOr<bool>, IsSrkRocaVulnerable, (), (override));
  MOCK_METHOD(StatusOr<brillo::Blob>, GetRsuDeviceId, (), (override));
  MOCK_METHOD(StatusOr<brillo::Blob>, GetIFXFieldUpgradeInfo, (), (override));
  MOCK_METHOD(Status, DeclareTpmFirmwareStable, (), (override));
  MOCK_METHOD(StatusOr<brillo::Blob>,
              SendRawCommand,
              (const brillo::Blob& command),
              (override));
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_MOCK_VENDOR_H_
