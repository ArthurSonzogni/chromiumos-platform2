// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_DAEMON_CALLBACK_H_
#define RMAD_DAEMON_CALLBACK_H_

#include <base/callback.h>

#include "rmad/proto_bindings/rmad.pb.h"

namespace rmad {

using HardwareVerificationSignalCallback =
    base::RepeatingCallback<void(const HardwareVerificationResult&)>;
using UpdateRoFirmwareSignalCallback =
    base::RepeatingCallback<void(UpdateRoFirmwareStatus)>;
using CalibrationOverallSignalCallback =
    base::RepeatingCallback<void(CalibrationOverallStatus)>;
using CalibrationComponentSignalCallback =
    base::RepeatingCallback<void(CalibrationComponentStatus)>;
using ProvisionSignalCallback =
    base::RepeatingCallback<void(const ProvisionStatus&)>;
using FinalizeSignalCallback =
    base::RepeatingCallback<void(const FinalizeStatus&)>;
using WriteProtectSignalCallback = base::RepeatingCallback<void(bool)>;
using PowerCableSignalCallback = base::RepeatingCallback<void(bool)>;

}  // namespace rmad

#endif  // RMAD_DAEMON_CALLBACK_H_
