// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_EC_COMMAND_FACTORY_H_
#define LIBEC_EC_COMMAND_FACTORY_H_

#include <memory>
#include <string>
#include <vector>

#include "biod/cros_fp_device_interface.h"
#include "libec/fingerprint/fp_context_command_factory.h"
#include "libec/fingerprint/fp_flashprotect_command.h"
#include "libec/fingerprint/fp_frame_command.h"
#include "libec/fingerprint/fp_info_command.h"
#include "libec/fingerprint/fp_seed_command.h"
#include "libec/fingerprint/fp_template_command.h"

namespace ec {

class EcCommandFactoryInterface {
 public:
  virtual ~EcCommandFactoryInterface() = default;

  virtual std::unique_ptr<EcCommandInterface> FpContextCommand(
      biod::CrosFpDeviceInterface* cros_fp, const std::string& user_id) = 0;

  virtual std::unique_ptr<FpFlashProtectCommand> FpFlashProtectCommand(
      const uint32_t flags, const uint32_t mask) = 0;
  static_assert(
      std::is_base_of<EcCommandInterface, ec::FpFlashProtectCommand>::value,
      "All commands created by this class should derive from "
      "EcCommandInterface");

  virtual std::unique_ptr<FpInfoCommand> FpInfoCommand() = 0;
  static_assert(std::is_base_of<EcCommandInterface, ec::FpInfoCommand>::value,
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
      std::vector<uint8_t> tmpl, uint16_t max_write_size) = 0;
  static_assert(
      std::is_base_of<EcCommandInterface, ec::FpTemplateCommand>::value,
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
      biod::CrosFpDeviceInterface* cros_fp,
      const std::string& user_id) override;

  std::unique_ptr<ec::FpFlashProtectCommand> FpFlashProtectCommand(
      const uint32_t flags, const uint32_t mask) override;

  std::unique_ptr<ec::FpInfoCommand> FpInfoCommand() override;

  std::unique_ptr<ec::FpSeedCommand> FpSeedCommand(
      const brillo::SecureVector& seed, uint16_t seed_version) override;

  std::unique_ptr<ec::FpFrameCommand> FpFrameCommand(
      int index, uint32_t frame_size, uint16_t max_read_size) override;

  std::unique_ptr<ec::FpTemplateCommand> FpTemplateCommand(
      std::vector<uint8_t> tmpl, uint16_t max_write_size) override;
};

}  // namespace ec

#endif  // LIBEC_EC_COMMAND_FACTORY_H_
