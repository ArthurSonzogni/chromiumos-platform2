// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_EC_COMMAND_FACTORY_H_
#define LIBEC_EC_COMMAND_FACTORY_H_

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "libec/charge_control_set_command.h"
#include "libec/charge_current_limit_set_command.h"
#include "libec/display_soc_command.h"
#include "libec/ec_command_version_supported.h"
#include "libec/fingerprint/fp_context_command_factory.h"
#include "libec/fingerprint/fp_frame_command.h"
#include "libec/fingerprint/fp_info_command.h"
#include "libec/fingerprint/fp_mode_command.h"
#include "libec/fingerprint/fp_seed_command.h"
#include "libec/fingerprint/fp_template_command.h"
#include "libec/flash_protect_command.h"
#include "libec/get_features_command.h"
#include "libec/get_protocol_info_command.h"
#include "libec/get_version_command.h"
#include "libec/i2c_read_command.h"
#include "libec/led_control_command.h"
#include "libec/motion_sense_command_lid_angle.h"
#include "libec/pwm/pwm_get_fan_target_rpm_command.h"
#include "libec/pwm/pwm_set_fan_target_rpm_command.h"
#include "libec/thermal/get_memmap_temp_b_command.h"
#include "libec/thermal/get_memmap_temp_command.h"
#include "libec/thermal/get_memmap_thermal_version_command.h"
#include "libec/thermal/temp_sensor_get_info_command.h"
#include "libec/thermal/thermal_auto_fan_ctrl_command.h"

namespace ec {

class EcCommandFactoryInterface {
 public:
  virtual ~EcCommandFactoryInterface() = default;

  virtual std::unique_ptr<EcCommandInterface> FpContextCommand(
      EcCommandVersionSupportedInterface* ec_cmd_ver_supported,
      const std::string& user_id) = 0;

  virtual std::unique_ptr<FlashProtectCommand> FlashProtectCommand(
      EcCommandVersionSupportedInterface* ec_cmd_ver_supported,
      flash_protect::Flags flags,
      flash_protect::Flags mask) = 0;
  static_assert(
      std::is_base_of<EcCommandInterface, ec::FlashProtectCommand>::value,
      "All commands created by this class should derive from "
      "EcCommandInterface");

  virtual std::unique_ptr<FpInfoCommand_v1> FpInfoCommand_v1(
      EcCommandVersionSupportedInterface* ec_cmd_ver_supported) = 0;
  static_assert(
      std::is_base_of<EcCommandInterface, ec::FpInfoCommand_v1>::value,
      "All commands created by this class should derive from "
      "EcCommandInterface");

  virtual std::unique_ptr<FpSeedCommand> FpSeedCommand(
      const brillo::SecureVector& seed, uint16_t seed_version) = 0;
  static_assert(std::is_base_of<EcCommandInterface, ec::FpSeedCommand>::value,
                "All commands created by this class should derive from "
                "EcCommandInterface");

  virtual std::unique_ptr<ec::FpFrameCommand> FpFrameCommand(
      int index, uint32_t frame_size, uint16_t max_read_size) = 0;
  static_assert(std::is_base_of<EcCommandInterface, ec::FpFrameCommand>::value,
                "All commands created by this class should derive from "
                "EcCommandInterface");

  virtual std::unique_ptr<ec::FpTemplateCommand> FpTemplateCommand(
      std::vector<uint8_t> tmpl, uint16_t max_write_size, bool commit) = 0;
  static_assert(
      std::is_base_of<EcCommandInterface, ec::FpTemplateCommand>::value,
      "All commands created by this class should derive from "
      "EcCommandInterface");

  virtual std::unique_ptr<ec::ChargeControlSetCommand> ChargeControlSetCommand(
      uint32_t mode, uint8_t lower, uint8_t upper) = 0;
  static_assert(
      std::is_base_of<EcCommandInterface, ec::ChargeControlSetCommand>::value,
      "All commands created by this class should derive from "
      "EcCommandInterface");

  virtual std::unique_ptr<ec::ChargeCurrentLimitSetCommand>
  ChargeCurrentLimitSetCommand(uint32_t limit_mA) = 0;
  static_assert(std::is_base_of<EcCommandInterface,
                                ec::ChargeCurrentLimitSetCommand>::value,
                "All commands created by this class should derive from "
                "EcCommandInterface");

  virtual std::unique_ptr<ec::DisplayStateOfChargeCommand>
  DisplayStateOfChargeCommand() = 0;
  static_assert(std::is_base_of<EcCommandInterface,
                                ec::DisplayStateOfChargeCommand>::value,
                "All commands created by this class should derive from "
                "EcCommandInterface");

  virtual std::unique_ptr<ec::GetFpModeCommand> GetFpModeCommand() = 0;
  static_assert(
      std::is_base_of<EcCommandInterface, ec::GetFpModeCommand>::value,
      "All commands created by this class should derive from "
      "EcCommandInterface");

  virtual std::unique_ptr<ec::FpModeCommand> FpModeCommand(FpMode mode) = 0;
  static_assert(std::is_base_of<EcCommandInterface, ec::FpModeCommand>::value,
                "All commands created by this class should derive from "
                "EcCommandInterface");

  virtual std::unique_ptr<ec::LedControlQueryCommand> LedControlQueryCommand(
      enum ec_led_id led_id) = 0;
  static_assert(
      std::is_base_of<EcCommandInterface, ec::LedControlQueryCommand>::value,
      "All commands created by this class should derive from "
      "EcCommandInterface");

  virtual std::unique_ptr<ec::LedControlSetCommand> LedControlSetCommand(
      enum ec_led_id led_id,
      std::array<uint8_t, EC_LED_COLOR_COUNT> brightness) = 0;
  static_assert(
      std::is_base_of<EcCommandInterface, ec::LedControlSetCommand>::value,
      "All commands created by this class should derive from "
      "EcCommandInterface");

  virtual std::unique_ptr<ec::LedControlAutoCommand> LedControlAutoCommand(
      enum ec_led_id led_id) = 0;
  static_assert(
      std::is_base_of<EcCommandInterface, ec::LedControlAutoCommand>::value,
      "All commands created by this class should derive from "
      "EcCommandInterface");

  virtual std::unique_ptr<ec::I2cReadCommand> I2cReadCommand(
      uint8_t port, uint8_t addr8, uint8_t offset, uint8_t read_len) = 0;
  static_assert(std::is_base_of<EcCommandInterface, ec::I2cReadCommand>::value,
                "All commands created by this class should derive from "
                "EcCommandInterface");

  virtual std::unique_ptr<ec::MotionSenseCommandLidAngle>
  MotionSenseCommandLidAngle() = 0;
  static_assert(std::is_base_of<EcCommandInterface,
                                ec::MotionSenseCommandLidAngle>::value,
                "All commands created by this class should derive from "
                "EcCommandInterface");

  virtual std::unique_ptr<ec::GetVersionCommand> GetVersionCommand() = 0;
  static_assert(
      std::is_base_of<EcCommandInterface, ec::GetVersionCommand>::value,
      "All commands created by this class should derive from "
      "EcCommandInterface");

  virtual std::unique_ptr<ec::GetProtocolInfoCommand>
  GetProtocolInfoCommand() = 0;
  static_assert(
      std::is_base_of<EcCommandInterface, ec::GetProtocolInfoCommand>::value,
      "All commands created by this class should derive from "
      "EcCommandInterface");

  virtual std::unique_ptr<ec::GetFeaturesCommand> GetFeaturesCommand() = 0;
  static_assert(
      std::is_base_of<EcCommandInterface, ec::GetFeaturesCommand>::value,
      "All commands created by this class should derive from "
      "EcCommandInterface");

  virtual std::unique_ptr<ec::PwmGetFanTargetRpmCommand>
  PwmGetFanTargetRpmCommand(uint8_t fan_idx) = 0;
  static_assert(
      std::is_base_of<EcCommandInterface, ec::PwmGetFanTargetRpmCommand>::value,
      "All commands created by this class should derive from "
      "EcCommandInterface");

  virtual std::unique_ptr<ec::PwmSetFanTargetRpmCommand>
  PwmSetFanTargetRpmCommand(uint32_t rpm, uint8_t fan_idx) = 0;
  static_assert(
      std::is_base_of<EcCommandInterface, ec::PwmSetFanTargetRpmCommand>::value,
      "All commands created by this class should derive from "
      "EcCommandInterface");

  virtual std::unique_ptr<ec::ThermalAutoFanCtrlCommand>
  ThermalAutoFanCtrlCommand(uint8_t fan_idx) = 0;
  static_assert(
      std::is_base_of<EcCommandInterface, ec::ThermalAutoFanCtrlCommand>::value,
      "All commands created by this class should derive from "
      "EcCommandInterface");

  virtual std::unique_ptr<ec::GetMemmapThermalVersionCommand>
  GetMemmapThermalVersionCommand() = 0;
  static_assert(std::is_base_of<EcCommandInterface,
                                ec::GetMemmapThermalVersionCommand>::value,
                "All commands created by this class should derive from "
                "EcCommandInterface");

  virtual std::unique_ptr<ec::GetMemmapTempCommand> GetMemmapTempCommand(
      uint8_t id) = 0;
  static_assert(
      std::is_base_of<EcCommandInterface, ec::GetMemmapTempCommand>::value,
      "All commands created by this class should derive from "
      "EcCommandInterface");

  virtual std::unique_ptr<ec::GetMemmapTempBCommand> GetMemmapTempBCommand(
      uint8_t id) = 0;
  static_assert(
      std::is_base_of<EcCommandInterface, ec::GetMemmapTempBCommand>::value,
      "All commands created by this class should derive from "
      "EcCommandInterface");

  virtual std::unique_ptr<ec::TempSensorGetInfoCommand>
  TempSensorGetInfoCommand(uint8_t id) = 0;
  static_assert(
      std::is_base_of<EcCommandInterface, ec::TempSensorGetInfoCommand>::value,
      "All commands created by this class should derive from "
      "EcCommandInterface");

  // TODO(b/144956297): Add factory methods for all of the EC
  // commands we use so that we can easily mock them for testing.
};

class BRILLO_EXPORT EcCommandFactory : public EcCommandFactoryInterface {
 public:
  EcCommandFactory() = default;
  ~EcCommandFactory() override = default;
  // Disallow copies
  EcCommandFactory(const EcCommandFactory&) = delete;
  EcCommandFactory& operator=(const EcCommandFactory&) = delete;

  std::unique_ptr<EcCommandInterface> FpContextCommand(
      EcCommandVersionSupportedInterface* ec_cmd_ver_supported,
      const std::string& user_id) override;

  std::unique_ptr<ec::FlashProtectCommand> FlashProtectCommand(
      EcCommandVersionSupportedInterface* ec_cmd_ver_supported,
      flash_protect::Flags flags,
      flash_protect::Flags mask) override;

  std::unique_ptr<ec::FpInfoCommand_v1> FpInfoCommand_v1(
      EcCommandVersionSupportedInterface* ec_cmd_ver_supported) override;

  std::unique_ptr<ec::FpSeedCommand> FpSeedCommand(
      const brillo::SecureVector& seed, uint16_t seed_version) override;

  std::unique_ptr<ec::FpFrameCommand> FpFrameCommand(
      int index, uint32_t frame_size, uint16_t max_read_size) override;

  std::unique_ptr<ec::FpTemplateCommand> FpTemplateCommand(
      std::vector<uint8_t> tmpl, uint16_t max_write_size, bool commit) override;

  std::unique_ptr<ec::ChargeControlSetCommand> ChargeControlSetCommand(
      uint32_t mode, uint8_t lower, uint8_t upper) override;

  std::unique_ptr<ec::ChargeCurrentLimitSetCommand>
  ChargeCurrentLimitSetCommand(uint32_t limit_mA) override;

  std::unique_ptr<ec::DisplayStateOfChargeCommand> DisplayStateOfChargeCommand()
      override;

  std::unique_ptr<ec::FpModeCommand> FpModeCommand(FpMode mode) override;

  std::unique_ptr<ec::GetFpModeCommand> GetFpModeCommand() override;

  std::unique_ptr<ec::I2cReadCommand> I2cReadCommand(uint8_t port,
                                                     uint8_t addr8,
                                                     uint8_t offset,
                                                     uint8_t read_len) override;

  std::unique_ptr<ec::LedControlQueryCommand> LedControlQueryCommand(
      enum ec_led_id led_id) override;

  std::unique_ptr<ec::LedControlSetCommand> LedControlSetCommand(
      enum ec_led_id led_id,
      std::array<uint8_t, EC_LED_COLOR_COUNT> brightness) override;

  std::unique_ptr<ec::LedControlAutoCommand> LedControlAutoCommand(
      enum ec_led_id led_id) override;

  std::unique_ptr<ec::MotionSenseCommandLidAngle> MotionSenseCommandLidAngle()
      override;

  std::unique_ptr<ec::GetVersionCommand> GetVersionCommand() override;

  std::unique_ptr<ec::GetProtocolInfoCommand> GetProtocolInfoCommand() override;

  std::unique_ptr<ec::GetFeaturesCommand> GetFeaturesCommand() override;

  std::unique_ptr<ec::PwmGetFanTargetRpmCommand> PwmGetFanTargetRpmCommand(
      uint8_t fan_idx) override;

  std::unique_ptr<ec::PwmSetFanTargetRpmCommand> PwmSetFanTargetRpmCommand(
      uint32_t rpm, uint8_t fan_idx) override;

  std::unique_ptr<ec::ThermalAutoFanCtrlCommand> ThermalAutoFanCtrlCommand(
      uint8_t fan_idx) override;

  std::unique_ptr<ec::GetMemmapThermalVersionCommand>
  GetMemmapThermalVersionCommand() override;

  std::unique_ptr<ec::GetMemmapTempCommand> GetMemmapTempCommand(
      uint8_t id) override;

  std::unique_ptr<ec::GetMemmapTempBCommand> GetMemmapTempBCommand(
      uint8_t id) override;

  std::unique_ptr<ec::TempSensorGetInfoCommand> TempSensorGetInfoCommand(
      uint8_t id) override;
};

}  // namespace ec

#endif  // LIBEC_EC_COMMAND_FACTORY_H_
