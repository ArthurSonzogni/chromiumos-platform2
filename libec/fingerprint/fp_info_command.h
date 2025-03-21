// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FINGERPRINT_FP_INFO_COMMAND_H_
#define LIBEC_FINGERPRINT_FP_INFO_COMMAND_H_

#include <memory>
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
  static constexpr int kDeadPixelsUnknown = -1;

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
  static constexpr int kDeadPixelsUnknown = -1;

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

}  // namespace ec

#endif  // LIBEC_FINGERPRINT_FP_INFO_COMMAND_H_
