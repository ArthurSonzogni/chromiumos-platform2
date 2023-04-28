// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FUNCTIONS_GENERIC_CAMERA_H_
#define RUNTIME_PROBE_FUNCTIONS_GENERIC_CAMERA_H_

#include <memory>

#include <base/values.h>

#include "runtime_probe/functions/mipi_camera.h"
#include "runtime_probe/functions/usb_camera.h"
#include "runtime_probe/probe_function.h"

namespace runtime_probe {

class GenericCameraFunction : public ProbeFunction {
  using ProbeFunction::ProbeFunction;

 public:
  NAME_PROBE_FUNCTION("generic_camera");

  template <typename T>
  static std::unique_ptr<T> FromKwargsValue(const base::Value& dict_value) {
    PARSE_BEGIN();
    instance->usb_prober_ = instance->GetUsbProber(dict_value);
    if (!instance->usb_prober_)
      return nullptr;
    instance->mipi_prober_ = instance->GetMipiProber(dict_value);
    if (!instance->mipi_prober_)
      return nullptr;
    PARSE_END();
  }

 private:
  // ProbeFunction overrides.
  bool PostParseArguments() override;
  DataType EvalImpl() const override;

  std::unique_ptr<UsbCameraFunction> usb_prober_;
  std::unique_ptr<MipiCameraFunction> mipi_prober_;

  // For mocking.
  virtual std::unique_ptr<UsbCameraFunction> GetUsbProber(
      const base::Value& dict_value) {
    return CreateProbeFunction<UsbCameraFunction>(dict_value);
  }
  virtual std::unique_ptr<MipiCameraFunction> GetMipiProber(
      const base::Value& dict_value) {
    return CreateProbeFunction<MipiCameraFunction>(dict_value);
  }
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_GENERIC_CAMERA_H_
