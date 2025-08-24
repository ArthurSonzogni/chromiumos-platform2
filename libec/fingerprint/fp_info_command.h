// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FINGERPRINT_FP_INFO_COMMAND_H_
#define LIBEC_FINGERPRINT_FP_INFO_COMMAND_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <brillo/brillo_export.h>

#include "libec/ec_command.h"
#include "libec/ec_command_async.h"
#include "libec/fingerprint/fp_info_params.h"
#include "libec/fingerprint/fp_sensor_errors.h"
#include "libec/fingerprint/sensor_id.h"
#include "libec/fingerprint/sensor_image.h"
#include "libec/fingerprint/template_info.h"

namespace ec {

class BRILLO_EXPORT FpInfoCommand_v1
    : public EcCommand<EmptyParam, struct ec_response_fp_info> {
 public:
  FpInfoCommand_v1() : EcCommand(EC_CMD_FP_INFO, kVersionOne) {}
  ~FpInfoCommand_v1() override = default;

  std::optional<SensorId> sensor_id();
  std::optional<SensorImage> sensor_image();
  std::optional<TemplateInfo> template_info();
  int NumDeadPixels();
  FpSensorErrors GetFpSensorErrors();

 private:
  std::optional<SensorId> sensor_id_;
  std::optional<SensorImage> sensor_image_;
  std::optional<TemplateInfo> template_info_;
};

class BRILLO_EXPORT FpInfoCommand_v2
    : public EcCommand<EmptyParam, struct fp_info::Params_v2> {
 public:
  FpInfoCommand_v2() : EcCommand(EC_CMD_FP_INFO, kVersionTwo) {}
  ~FpInfoCommand_v2() override = default;

  std::optional<SensorId> sensor_id();
  std::vector<SensorImage> sensor_image();
  std::optional<TemplateInfo> template_info();
  int NumDeadPixels();
  FpSensorErrors GetFpSensorErrors();

 private:
  std::optional<SensorId> sensor_id_;
  std::vector<SensorImage> sensor_image_;
  std::optional<TemplateInfo> template_info_;
};

class BRILLO_EXPORT FpInfoCommand : public EcCommandInterface {
 public:
  static constexpr int kDeadPixelsUnknown = -1;

  explicit FpInfoCommand(uint32_t version) : command_version(version) {
    CHECK_GT(version, 0);
    CHECK_LE(version, 2);
    if (version == 2) {
      fp_info_command_v2_ = std::make_unique<FpInfoCommand_v2>();
    } else {
      fp_info_command_v1_ = std::make_unique<FpInfoCommand_v1>();
    }
  }

  // Only for testing.
  FpInfoCommand(uint32_t version,
                std::unique_ptr<FpInfoCommand_v1> v1,
                std::unique_ptr<FpInfoCommand_v2> v2)
      : command_version(version) {
    CHECK_GT(version, 0);
    CHECK_LE(version, 2);
    if (version == 2) {
      CHECK_EQ(v1, nullptr);
      fp_info_command_v2_ = std::move(v2);
    } else {
      fp_info_command_v1_ = std::move(v1);
      CHECK_EQ(v2, nullptr);
    }
  }

  bool Run(int ec_fd) override;
  bool Run(ec::EcUsbEndpointInterface& uep) override;
  bool RunWithMultipleAttempts(int fd, int num_attempts) override;
  uint32_t Version() const override;
  uint32_t Command() const override;

  uint32_t Result() const;
  uint32_t GetVersion() const;

  virtual std::optional<SensorId> sensor_id();
  virtual std::vector<SensorImage> sensor_image();
  virtual std::optional<TemplateInfo> template_info();
  virtual int NumDeadPixels();
  virtual FpSensorErrors GetFpSensorErrors();
  std::string ParseSensorInfo();

 private:
  std::unique_ptr<FpInfoCommand_v1> fp_info_command_v1_ = nullptr;
  std::unique_ptr<FpInfoCommand_v2> fp_info_command_v2_ = nullptr;
  uint32_t command_version;
};

}  // namespace ec

#endif  // LIBEC_FINGERPRINT_FP_INFO_COMMAND_H_
