// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <vector>

#include "libec/ec_command_factory.h"
#include "libec/fingerprint/fp_flashprotect_command.h"
#include "libec/fingerprint/fp_info_command.h"
#include "libec/fingerprint/fp_template_command.h"

namespace ec {

std::unique_ptr<EcCommandInterface> EcCommandFactory::FpContextCommand(
    biod::CrosFpDeviceInterface* cros_fp, const std::string& user_id) {
  return FpContextCommandFactory::Create(cros_fp, user_id);
}

std::unique_ptr<FpFlashProtectCommand> EcCommandFactory::FpFlashProtectCommand(
    const uint32_t flags, const uint32_t mask) {
  return FpFlashProtectCommand::Create(flags, mask);
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
    std::vector<uint8_t> tmpl, uint16_t max_write_size) {
  return FpTemplateCommand::Create(std::move(tmpl), max_write_size);
}

}  // namespace ec
