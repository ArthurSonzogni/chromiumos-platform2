// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_MOCK_EC_COMMAND_FACTORY_H_
#define LIBEC_MOCK_EC_COMMAND_FACTORY_H_

#include <array>
#include <memory>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <libec/ec_command_factory.h>

namespace ec {

class MockEcCommandFactory : public ec::EcCommandFactoryInterface {
 public:
  MockEcCommandFactory() = default;
  ~MockEcCommandFactory() override = default;

  MOCK_METHOD(std::unique_ptr<ec::EcCommandInterface>,
              FpContextCommand,
              (EcCommandVersionSupportedInterface * ec_cmd_ver_supported,
               const std::string& user_id),
              (override));
  MOCK_METHOD(std::unique_ptr<ec::FlashProtectCommand>,
              FlashProtectCommand,
              (EcCommandVersionSupportedInterface * ec_cmd_ver_supported,
               flash_protect::Flags flags,
               flash_protect::Flags mask),
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
  MOCK_METHOD(std::unique_ptr<ec::FpTemplateCommand>,
              FpTemplateCommand,
              (std::vector<uint8_t> tmpl, uint16_t max_write_size, bool commit),
              (override));
  MOCK_METHOD(std::unique_ptr<ec::ChargeControlSetCommand>,
              ChargeControlSetCommand,
              (uint32_t mode, uint8_t lower, uint8_t upper),
              (override));
  MOCK_METHOD(std::unique_ptr<ec::ChargeCurrentLimitSetCommand>,
              ChargeCurrentLimitSetCommand,
              (uint32_t limit_mA),
              (override));
  MOCK_METHOD(std::unique_ptr<ec::DisplayStateOfChargeCommand>,
              DisplayStateOfChargeCommand,
              (),
              (override));
  MOCK_METHOD(std::unique_ptr<ec::FpModeCommand>,
              FpModeCommand,
              (FpMode mode),
              (override));
  MOCK_METHOD(std::unique_ptr<ec::GetFpModeCommand>,
              GetFpModeCommand,
              (),
              (override));
  MOCK_METHOD(std::unique_ptr<ec::LedControlQueryCommand>,
              LedControlQueryCommand,
              (enum ec_led_id led_id),
              (override));
  MOCK_METHOD(std::unique_ptr<ec::LedControlSetCommand>,
              LedControlSetCommand,
              (enum ec_led_id led_id,
               (std::array<uint8_t, EC_LED_COLOR_COUNT> brightness)),
              (override));
  MOCK_METHOD(std::unique_ptr<ec::LedControlAutoCommand>,
              LedControlAutoCommand,
              (enum ec_led_id led_id),
              (override));
  MOCK_METHOD(std::unique_ptr<ec::I2cReadCommand>,
              I2cReadCommand,
              (uint8_t port, uint8_t addr8, uint8_t offset, uint8_t read_len),
              (override));
  MOCK_METHOD(std::unique_ptr<ec::MotionSenseCommandLidAngle>,
              MotionSenseCommandLidAngle,
              (),
              (override));
  MOCK_METHOD(std::unique_ptr<ec::GetVersionCommand>,
              GetVersionCommand,
              (),
              (override));
  MOCK_METHOD(std::unique_ptr<ec::GetProtocolInfoCommand>,
              GetProtocolInfoCommand,
              (),
              (override));
  MOCK_METHOD(std::unique_ptr<ec::GetFeaturesCommand>,
              GetFeaturesCommand,
              (),
              (override));
  MOCK_METHOD(std::unique_ptr<ec::PwmGetFanTargetRpmCommand>,
              PwmGetFanTargetRpmCommand,
              (uint8_t fan_idx),
              (override));
  MOCK_METHOD(std::unique_ptr<ec::PwmSetFanTargetRpmCommand>,
              PwmSetFanTargetRpmCommand,
              (uint32_t rpm, uint8_t fan_idx),
              (override));
  MOCK_METHOD(std::unique_ptr<ec::ThermalAutoFanCtrlCommand>,
              ThermalAutoFanCtrlCommand,
              (uint8_t fan_idx),
              (override));
  MOCK_METHOD(std::unique_ptr<ec::GetMemmapThermalVersionCommand>,
              GetMemmapThermalVersionCommand,
              (),
              (override));
  MOCK_METHOD(std::unique_ptr<ec::GetMemmapTempCommand>,
              GetMemmapTempCommand,
              (uint8_t id),
              (override));
  MOCK_METHOD(std::unique_ptr<ec::GetMemmapTempBCommand>,
              GetMemmapTempBCommand,
              (uint8_t id),
              (override));
  MOCK_METHOD(std::unique_ptr<ec::TempSensorGetInfoCommand>,
              TempSensorGetInfoCommand,
              (uint8_t id),
              (override));
};

}  // namespace ec

#endif  // LIBEC_MOCK_EC_COMMAND_FACTORY_H_
