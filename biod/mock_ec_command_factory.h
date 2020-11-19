// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_MOCK_EC_COMMAND_FACTORY_H_
#define BIOD_MOCK_EC_COMMAND_FACTORY_H_

#include <memory>
#include <string>

#include <gmock/gmock.h>
#include <libec/ec_command_factory.h>

namespace biod {

class MockEcCommandFactory : public ec::EcCommandFactoryInterface {
 public:
  MockEcCommandFactory() = default;
  ~MockEcCommandFactory() override = default;

  MOCK_METHOD(std::unique_ptr<ec::EcCommandInterface>,
              FpContextCommand,
              (CrosFpDeviceInterface * cros_fp, const std::string& user_id),
              (override));
  MOCK_METHOD(std::unique_ptr<ec::FpFlashProtectCommand>,
              FpFlashProtectCommand,
              (const uint32_t flags, const uint32_t mask),
              (override));
  MOCK_METHOD(std::unique_ptr<ec::FpInfoCommand>,
              FpInfoCommand,
              (),
              (override));
  MOCK_METHOD(std::unique_ptr<ec::FpSeedCommand>,
              FpSeedCommand,
              (const brillo::SecureVector& seed, uint16_t seed_version),
              (override));
  MOCK_METHOD(std::unique_ptr<ec::FpFrameCommand>,
              FpFrameCommand,
              (int index, uint32_t frame_size, uint16_t max_read_size),
              (override));
};

}  // namespace biod

#endif  // BIOD_MOCK_EC_COMMAND_FACTORY_H_
