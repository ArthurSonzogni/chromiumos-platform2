// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/executor/delegate_impl.h"

#include <fcntl.h>
#include <utility>

#include <base/logging.h>
#include <chromeos/ec/ec_commands.h>
#include <libec/fingerprint/fp_frame_command.h>
#include <libec/fingerprint/fp_info_command.h>
#include <libec/fingerprint/fp_mode_command.h>
#include <libec/get_protocol_info_command.h>
#include <libec/get_version_command.h>
#include <libec/mkbp_event.h>

#include "diagnostics/cros_healthd/executor/constants.h"
#include "diagnostics/cros_healthd/executor/mojom/executor.mojom.h"

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

ec::FpMode ToEcFpMode(mojom::FingerprintCaptureType type) {
  switch (type) {
    case mojom::FingerprintCaptureType::kCheckerboardTest:
      return ec::FpMode(ec::FpMode::Mode::kCapturePattern0);
    case mojom::FingerprintCaptureType::kInvertedCheckerboardTest:
      return ec::FpMode(ec::FpMode::Mode::kCapturePattern1);
    case mojom::FingerprintCaptureType::kResetTest:
      return ec::FpMode(ec::FpMode::Mode::kCaptureResetTest);
  }
}

}  // namespace

namespace diagnostics {

DelegateImpl::DelegateImpl() {}
DelegateImpl::~DelegateImpl() {}

void DelegateImpl::GetFingerprintFrame(mojom::FingerprintCaptureType type,
                                       GetFingerprintFrameCallback callback) {
  auto result = mojom::FingerprintFrameResult::New();
  auto cros_fd = base::ScopedFD(open(fingerprint::kCrosFpPath, O_RDWR));

  ec::FpInfoCommand info;
  if (!info.Run(cros_fd.get())) {
    std::move(callback).Run(std::move(result),
                            "Failed to run ec::FpInfoCommand");
    return;
  }

  result->width = info.sensor_image()->width;
  result->height = info.sensor_image()->height;

  ec::MkbpEvent mkbp_event(cros_fd.get(), EC_MKBP_EVENT_FINGERPRINT);
  if (mkbp_event.Enable() != 0) {
    PLOG(ERROR) << "Failed to enable fingerprint event";
    std::move(callback).Run(std::move(result),
                            "Failed to enable fingerprint event");
    return;
  }

  ec::FpModeCommand fp_mode_cmd(ToEcFpMode(type));
  if (!fp_mode_cmd.Run(cros_fd.get())) {
    std::move(callback).Run(std::move(result), "Failed to set capture mode");
    return;
  }

  // Wait for EC fingerprint event. Once it's done, it means the "capture"
  // action is completed, so we can get fingerprint frame data safely.
  //
  // We'll wait for 5 seconds until timeout. It blocks the process here but it's
  // okay for both caller and callee.
  //   - Callee is here, the delegate process, which only does one job for each
  //   launch, once it's done, it'll be terminated from the caller side.
  //   - Caller is the executor process, which uses async interface to
  //   communicate with delegate process.
  int rv = mkbp_event.Wait(5000);
  if (rv != 1) {
    PLOG(ERROR) << "Failed to poll fingerprint event after 5 seconds";
    std::move(callback).Run(std::move(result),
                            "Failed to poll fingerprint event after 5 seconds");
    return;
  }

  ec::GetProtocolInfoCommand ec_protocol_cmd;
  if (!ec_protocol_cmd.RunWithMultipleAttempts(cros_fd.get(), 2)) {
    std::move(callback).Run(std::move(result),
                            "Failed to get EC protocol info");
    return;
  }

  uint32_t size = result->width * result->height;
  if (size == 0) {
    std::move(callback).Run(std::move(result), "Frame size is zero");
    return;
  }

  auto fp_frame_command = ec::FpFrameCommand::Create(
      FP_FRAME_INDEX_RAW_IMAGE, size, ec_protocol_cmd.MaxReadBytes());
  if (!fp_frame_command) {
    std::move(callback).Run(std::move(result),
                            "Failed to create fingerprint frame command");
    return;
  }

  if (!fp_frame_command->Run(cros_fd.get())) {
    std::move(callback).Run(std::move(result),
                            "Failed to get fingerprint frame");
    return;
  }

  result->frame = std::move(*fp_frame_command->frame());

  if (result->width * result->height != result->frame.size()) {
    std::move(callback).Run(std::move(result),
                            "Frame size is not equal to width * height");
    return;
  }

  std::move(callback).Run(std::move(result), std::nullopt);
}

void DelegateImpl::GetFingerprintInfo(GetFingerprintInfoCallback callback) {
  auto result = mojom::FingerprintInfoResult::New();
  auto cros_fd = base::ScopedFD(open(fingerprint::kCrosFpPath, O_RDWR));

  ec::GetVersionCommand version;
  if (!version.Run(cros_fd.get())) {
    std::move(callback).Run(std::move(result),
                            "Failed to get fingerprint version");
    return;
  }

  result->rw_fw = version.Image() == EC_IMAGE_RW;

  std::move(callback).Run(std::move(result), std::nullopt);
}

}  // namespace diagnostics
