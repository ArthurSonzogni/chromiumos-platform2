// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/ec_command_factory.h"

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "libec/fingerprint/fp_info_command.h"
#include "libec/fingerprint/fp_template_command.h"
#include "libec/flash_protect_command_factory.h"

namespace ec {

std::unique_ptr<EcCommandInterface> EcCommandFactory::FpContextCommand(
    CrosFpDeviceInterface* cros_fp, const std::string& user_id) {
  return FpContextCommandFactory::Create(cros_fp, user_id);
}

std::unique_ptr<FlashProtectCommand> EcCommandFactory::FlashProtectCommand(
    CrosFpDeviceInterface* cros_fp,
    flash_protect::Flags flags,
    flash_protect::Flags mask) {
  return FlashProtectCommandFactory::Create(cros_fp, flags, mask);
}

std::unique_ptr<FpInfoCommand> EcCommandFactory::FpInfoCommand() {
  return std::make_unique<ec::FpInfoCommand>();
}

std::unique_ptr<ec::FpFrameCommand> EcCommandFactory::FpFrameCommand(
    int index, uint32_t frame_size, uint16_t max_read_size) {
  return FpFrameCommand::Create(index, frame_size, max_read_size);
}

std::unique_ptr<ec::FpSeedCommand> EcCommandFactory::FpSeedCommand(
    const brillo::SecureVector& seed, uint16_t seed_version) {
  return FpSeedCommand::Create(seed, seed_version);
}

std::unique_ptr<ec::FpTemplateCommand> EcCommandFactory::FpTemplateCommand(
    std::vector<uint8_t> tmpl, uint16_t max_write_size, bool commit) {
  return FpTemplateCommand::Create(std::move(tmpl), max_write_size, commit);
}

std::unique_ptr<ec::FpUnlockTemplateCommand>
EcCommandFactory::FpUnlockTemplateCommand(uint16_t finger_num) {
  return FpUnlockTemplateCommand::Create(finger_num);
}

std::unique_ptr<ec::FpMigrateTemplateToNonceContextCommand>
EcCommandFactory::FpMigrateTemplateToNonceContextCommand(
    const std::string& user_id) {
  return FpMigrateTemplateToNonceContextCommand::Create(user_id);
}

std::unique_ptr<ec::ChargeControlSetCommand>
EcCommandFactory::ChargeControlSetCommand(uint32_t mode,
                                          uint8_t lower,
                                          uint8_t upper) {
  return std::make_unique<ec::ChargeControlSetCommand>(mode, lower, upper);
}

std::unique_ptr<ec::ChargeCurrentLimitSetCommand>
EcCommandFactory::ChargeCurrentLimitSetCommand(uint32_t limit_mA) {
  return std::make_unique<ec::ChargeCurrentLimitSetCommand>(limit_mA);
}

std::unique_ptr<ec::DisplayStateOfChargeCommand>
EcCommandFactory::DisplayStateOfChargeCommand() {
  return std::make_unique<ec::DisplayStateOfChargeCommand>();
}

std::unique_ptr<ec::FpGetNonceCommand> EcCommandFactory::FpGetNonceCommand() {
  return std::make_unique<ec::FpGetNonceCommand>();
}

std::unique_ptr<ec::FpSetNonceContextCommand>
EcCommandFactory::FpSetNonceContextCommand(
    const brillo::Blob& nonce,
    const brillo::Blob& encrypted_user_id,
    const brillo::Blob& iv) {
  return FpSetNonceContextCommand::Create(nonce, encrypted_user_id, iv);
}

std::unique_ptr<ec::FpReadMatchSecretWithPubkeyCommand>
EcCommandFactory::FpReadMatchSecretWithPubkeyCommand(
    uint16_t index, const brillo::Blob& pk_in_x, const brillo::Blob& pk_in_y) {
  return FpReadMatchSecretWithPubkeyCommand::Create(index, pk_in_x, pk_in_y);
}

std::unique_ptr<ec::FpPairingKeyKeygenCommand>
EcCommandFactory::FpPairingKeyKeygenCommand() {
  return std::make_unique<ec::FpPairingKeyKeygenCommand>();
}

std::unique_ptr<ec::FpPairingKeyLoadCommand>
EcCommandFactory::FpPairingKeyLoadCommand(
    const brillo::Blob& encrypted_pairing_key) {
  return FpPairingKeyLoadCommand::Create(encrypted_pairing_key);
}

std::unique_ptr<ec::FpPairingKeyWrapCommand>
EcCommandFactory::FpPairingKeyWrapCommand(const brillo::Blob& pub_x,
                                          const brillo::Blob& pub_y,
                                          const brillo::Blob& encrypted_priv) {
  return FpPairingKeyWrapCommand::Create(pub_x, pub_y, encrypted_priv);
}

std::unique_ptr<ec::FpModeCommand> EcCommandFactory::FpModeCommand(
    FpMode mode) {
  return std::make_unique<ec::FpModeCommand>(mode);
}

std::unique_ptr<ec::LedControlQueryCommand>
EcCommandFactory::LedControlQueryCommand(enum ec_led_id led_id) {
  return std::make_unique<ec::LedControlQueryCommand>(led_id);
}

std::unique_ptr<ec::LedControlSetCommand>
EcCommandFactory::LedControlSetCommand(
    enum ec_led_id led_id, std::array<uint8_t, EC_LED_COLOR_COUNT> brightness) {
  return std::make_unique<ec::LedControlSetCommand>(led_id, brightness);
}

std::unique_ptr<ec::LedControlAutoCommand>
EcCommandFactory::LedControlAutoCommand(enum ec_led_id led_id) {
  return std::make_unique<ec::LedControlAutoCommand>(led_id);
}

std::unique_ptr<ec::I2cReadCommand> EcCommandFactory::I2cReadCommand(
    uint8_t port, uint8_t addr8, uint8_t offset, uint8_t read_len) {
  return ec::I2cReadCommand::Create(port, addr8, offset, read_len);
}

std::unique_ptr<ec::MotionSenseCommandLidAngle>
EcCommandFactory::MotionSenseCommandLidAngle() {
  return std::make_unique<ec::MotionSenseCommandLidAngle>();
}

std::unique_ptr<ec::GetVersionCommand> EcCommandFactory::GetVersionCommand() {
  return std::make_unique<ec::GetVersionCommand>();
}

std::unique_ptr<ec::GetProtocolInfoCommand>
EcCommandFactory::GetProtocolInfoCommand() {
  return std::make_unique<ec::GetProtocolInfoCommand>();
}

std::unique_ptr<ec::GetFeaturesCommand> EcCommandFactory::GetFeaturesCommand() {
  return std::make_unique<ec::GetFeaturesCommand>();
}

std::unique_ptr<ec::PwmGetFanTargetRpmCommand>
EcCommandFactory::PwmGetFanTargetRpmCommand(uint8_t fan_idx) {
  return std::make_unique<ec::PwmGetFanTargetRpmCommand>(fan_idx);
}

std::unique_ptr<ec::PwmSetFanTargetRpmCommand>
EcCommandFactory::PwmSetFanTargetRpmCommand(uint32_t rpm, uint8_t fan_idx) {
  return std::make_unique<ec::PwmSetFanTargetRpmCommand>(rpm, fan_idx);
}

std::unique_ptr<ec::ThermalAutoFanCtrlCommand>
EcCommandFactory::ThermalAutoFanCtrlCommand(uint8_t fan_idx) {
  return std::make_unique<ec::ThermalAutoFanCtrlCommand>(fan_idx);
}

std::unique_ptr<ec::GetMemmapThermalVersionCommand>
EcCommandFactory::GetMemmapThermalVersionCommand() {
  return std::make_unique<ec::GetMemmapThermalVersionCommand>();
}

std::unique_ptr<ec::GetMemmapTempCommand>
EcCommandFactory::GetMemmapTempCommand(uint8_t id) {
  return std::make_unique<ec::GetMemmapTempCommand>(id);
}

std::unique_ptr<ec::GetMemmapTempBCommand>
EcCommandFactory::GetMemmapTempBCommand(uint8_t id) {
  return std::make_unique<ec::GetMemmapTempBCommand>(id);
}

std::unique_ptr<ec::TempSensorGetInfoCommand>
EcCommandFactory::TempSensorGetInfoCommand(uint8_t id) {
  return std::make_unique<ec::TempSensorGetInfoCommand>(id);
}

}  // namespace ec
