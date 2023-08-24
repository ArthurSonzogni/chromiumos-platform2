// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/generic_camera.h"
#include "runtime_probe/functions/mipi_camera.h"
#include "runtime_probe/functions/usb_camera.h"
#include "runtime_probe/probe_function.h"

namespace runtime_probe {

bool GenericCameraFunction::PostParseArguments() {
  runner_.AddFunction(CreateProbeFunction<UsbCameraFunction>());
  runner_.AddFunction(CreateProbeFunction<MipiCameraFunction>());
  return runner_.IsValid();
}

}  // namespace runtime_probe
