// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FRONTEND_CLIENT_MOCK_FRONTEND_H_
#define LIBHWSEC_FRONTEND_CLIENT_MOCK_FRONTEND_H_

#include <optional>
#include <vector>

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

#include "libhwsec/frontend/client/frontend.h"
#include "libhwsec/frontend/mock_frontend.h"

namespace hwsec {

class MockClientFrontend : public MockFrontend, public ClientFrontend {
 public:
  MockClientFrontend() = default;
  ~MockClientFrontend() override = default;

  MOCK_METHOD(StatusOr<brillo::Blob>, GetRandomBlob, (size_t size), (override));
  MOCK_METHOD(StatusOr<bool>, IsSrkRocaVulnerable, (), (override));
  MOCK_METHOD(StatusOr<uint32_t>, GetFamily, (), (override));
  MOCK_METHOD(StatusOr<uint64_t>, GetSpecLevel, (), (override));
  MOCK_METHOD(StatusOr<uint32_t>, GetManufacturer, (), (override));
  MOCK_METHOD(StatusOr<uint32_t>, GetTpmModel, (), (override));
  MOCK_METHOD(StatusOr<uint64_t>, GetFirmwareVersion, (), (override));
  MOCK_METHOD(StatusOr<brillo::Blob>, GetVendorSpecific, (), (override));
  MOCK_METHOD(StatusOr<IFXFieldUpgradeInfo>,
              GetIFXFieldUpgradeInfo,
              (),
              (override));
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_CLIENT_MOCK_FRONTEND_H_
