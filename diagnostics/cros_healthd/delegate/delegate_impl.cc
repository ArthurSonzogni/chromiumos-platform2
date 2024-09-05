// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/delegate_impl.h"

#include <fcntl.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/posix/eintr_wrapper.h>
#include <base/task/single_thread_task_runner.h>
#include <base/threading/thread.h>
#include <base/time/time.h>
#include <brillo/udev/udev.h>
#include <chromeos/ec/ec_commands.h>
#include <libec/ec_command_factory.h>
#include <libec/fingerprint/fp_frame_command.h>
#include <libec/fingerprint/fp_info_command.h>
#include <libec/fingerprint/fp_mode_command.h>
#include <libec/get_features_command.h>
#include <libec/get_protocol_info_command.h>
#include <libec/get_version_command.h>
#include <libec/i2c_read_command.h>
#include <libec/led_control_command.h>
#include <libec/mkbp_event.h>
#include <libec/motion_sense_command_lid_angle.h>
#include <libec/pwm/pwm_get_fan_target_rpm_command.h>
#include <libec/pwm/pwm_set_fan_target_rpm_command.h>
#include <libec/thermal/thermal_auto_fan_ctrl_command.h>

#include "diagnostics/cros_healthd/delegate/constants.h"
#include "diagnostics/cros_healthd/delegate/events/audio_jack_evdev_delegate.h"
#include "diagnostics/cros_healthd/delegate/events/power_button_evdev_delegate.h"
#include "diagnostics/cros_healthd/delegate/events/stylus_evdev_delegate.h"
#include "diagnostics/cros_healthd/delegate/events/stylus_garage_evdev_delegate.h"
#include "diagnostics/cros_healthd/delegate/events/touchpad_evdev_delegate.h"
#include "diagnostics/cros_healthd/delegate/events/touchscreen_evdev_delegate.h"
#include "diagnostics/cros_healthd/delegate/events/volume_button_evdev_delegate.h"
#include "diagnostics/cros_healthd/delegate/fetchers/boot_performance.h"
#include "diagnostics/cros_healthd/delegate/fetchers/display_fetcher.h"
#include "diagnostics/cros_healthd/delegate/fetchers/graphics_fetcher.h"
#include "diagnostics/cros_healthd/delegate/fetchers/psr_fetcher.h"
#include "diagnostics/cros_healthd/delegate/fetchers/thermal_fetcher.h"
#include "diagnostics/cros_healthd/delegate/fetchers/touchpad_fetcher.h"
#include "diagnostics/cros_healthd/delegate/routines/cpu_routine_task_delegate.h"
#include "diagnostics/cros_healthd/delegate/routines/floating_point_accuracy.h"
#include "diagnostics/cros_healthd/delegate/routines/prime_number_search_delegate_impl.h"
#include "diagnostics/cros_healthd/delegate/routines/urandom_delegate.h"
#include "diagnostics/cros_healthd/delegate/utils/display_util.h"
#include "diagnostics/cros_healthd/delegate/utils/display_util_factory.h"
#include "diagnostics/cros_healthd/delegate/utils/evdev_monitor.h"
#include "diagnostics/cros_healthd/delegate/utils/ndt_client.h"
#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

// The maximum number of times we will retry getting external display info.
constexpr int kMaximumGetExternalDisplayInfoRetry = 10;

// The interval to wait between retrying to get external display info.
constexpr base::TimeDelta kGetExternalDisplayInfoRetryPeriod =
    base::Milliseconds(500);

// The i2c address defined at platform/ec/include/battery_smart.h is 7-bit i2c
// address, which is 0x0B (BATTERY_ADDR_FLAGS). We should pass the 8-bit i2c
// address, which is 0x16, to libec.
constexpr uint8_t kBatteryI2cAddress = 0x16;

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

enum ec_led_id ToEcLedId(mojom::LedName name) {
  switch (name) {
    case mojom::LedName::kBattery:
      return EC_LED_ID_BATTERY_LED;
    case mojom::LedName::kPower:
      return EC_LED_ID_POWER_LED;
    case mojom::LedName::kAdapter:
      return EC_LED_ID_ADAPTER_LED;
    case mojom::LedName::kLeft:
      return EC_LED_ID_LEFT_LED;
    case mojom::LedName::kRight:
      return EC_LED_ID_RIGHT_LED;
    case mojom::LedName::kUnmappedEnumField:
      LOG(WARNING) << "LedName UnmappedEnumField";
      return EC_LED_ID_COUNT;
  }
}

enum ec_led_colors ToEcLedColor(mojom::LedColor color) {
  switch (color) {
    case mojom::LedColor::kRed:
      return EC_LED_COLOR_RED;
    case mojom::LedColor::kGreen:
      return EC_LED_COLOR_GREEN;
    case mojom::LedColor::kBlue:
      return EC_LED_COLOR_BLUE;
    case mojom::LedColor::kYellow:
      return EC_LED_COLOR_YELLOW;
    case mojom::LedColor::kWhite:
      return EC_LED_COLOR_WHITE;
    case mojom::LedColor::kAmber:
      return EC_LED_COLOR_AMBER;
    case mojom::LedColor::kUnmappedEnumField:
      LOG(WARNING) << "LedColor UnmappedEnumField";
      return EC_LED_COLOR_COUNT;
  }
}

// A common util function to read the number of fans in the device.
std::optional<uint8_t> GetNumFans(
    ec::EcCommandFactoryInterface* ec_command_factory, const int cros_fd) {
  std::unique_ptr<ec::GetFeaturesCommand> get_features =
      ec_command_factory->GetFeaturesCommand();
  if (!get_features || !get_features->Run(cros_fd)) {
    LOG(ERROR) << "Failed to run ec::GetFeaturesCommand";
    return std::nullopt;
  }

  if (!get_features->IsFeatureSupported(EC_FEATURE_PWM_FAN)) {
    return 0;
  }

  static_assert(EC_FAN_SPEED_ENTRIES < std::numeric_limits<uint8_t>::max(),
                "Value of EC_FAN_SPEED_ENTRIES exceeds maximum value of uint8");

  uint8_t fan_idx;
  for (fan_idx = 0; fan_idx < EC_FAN_SPEED_ENTRIES; ++fan_idx) {
    std::unique_ptr<ec::PwmGetFanTargetRpmCommand> get_fan_rpm =
        ec_command_factory->PwmGetFanTargetRpmCommand(fan_idx);
    if (!get_fan_rpm || !get_fan_rpm->Run(cros_fd) ||
        !get_fan_rpm->Rpm().has_value()) {
      LOG(ERROR) << "Failed to read fan speed for fan idx: "
                 << static_cast<int>(fan_idx);
      return std::nullopt;
    }

    if (get_fan_rpm->Rpm().value() == EC_FAN_SPEED_NOT_PRESENT)
      return fan_idx;
  }
  return fan_idx;
}

bool HasMissingDrmField(
    const mojom::ExternalDisplayInfoPtr& external_display_info) {
  // Check for display size.
  if (external_display_info->display_width.is_null() ||
      external_display_info->display_height.is_null()) {
    return true;
  }

  // Check for resolution.
  if (external_display_info->resolution_horizontal.is_null() ||
      external_display_info->resolution_vertical.is_null()) {
    return true;
  }

  // Check for refresh rate.
  if (external_display_info->refresh_rate.is_null()) {
    return true;
  }

  // Check for EDID information.
  if (!external_display_info->edid_version.has_value()) {
    return true;
  }

  return false;
}

}  // namespace

namespace diagnostics {
namespace {

void MonitorEvdevEvents(std::unique_ptr<EvdevMonitor::Delegate> delegate,
                        bool allow_multiple_devices = false) {
  // Long-run method. The following object keeps alive until the process
  // terminates.
  EvdevMonitor* evdev_monitor = new EvdevMonitor(std::move(delegate));
  evdev_monitor->StartMonitoring(allow_multiple_devices);
}

bool RunCpuTaskRoutine(std::unique_ptr<CpuRoutineTaskDelegate> task_delegate,
                       base::TimeDelta exec_duration) {
  if (!task_delegate) {
    return false;
  }

  base::TimeTicks end_time = base::TimeTicks::Now() + exec_duration;
  while (base::TimeTicks::Now() < end_time) {
    if (!task_delegate->Run()) {
      return false;
    }
  }
  return true;
}

void GetConnectedExternalDisplayConnectorsHelper(
    DisplayUtilFactory* display_util_factory,
    std::optional<std::vector<uint32_t>> last_known_connectors,
    DelegateImpl::GetConnectedExternalDisplayConnectorsCallback callback,
    int times) {
  std::unique_ptr<DisplayUtil> display_util = display_util_factory->Create();
  if (!display_util) {
    std::move(callback).Run(
        base::flat_map<uint32_t, mojom::ExternalDisplayInfoPtr>{},
        "Failed to create DisplayUtil");
    return;
  }

  base::flat_map<uint32_t, mojom::ExternalDisplayInfoPtr>
      external_display_connectors;

  std::vector<uint32_t> connector_ids =
      display_util->GetExternalDisplayConnectorIDs();

  if (last_known_connectors.has_value()) {
    std::sort(connector_ids.begin(), connector_ids.end());
    // If the connected connectors are identical to the previous state, it is
    // possible that DRM have not detected the new display yet. Retry to ensure
    // that all DRM changes are detected.
    if (last_known_connectors == connector_ids &&
        times < kMaximumGetExternalDisplayInfoRetry) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&GetConnectedExternalDisplayConnectorsHelper,
                         display_util_factory, last_known_connectors,
                         std::move(callback), times + 1),
          kGetExternalDisplayInfoRetryPeriod);
      return;
    }
  }

  for (const auto& connector_id : connector_ids) {
    external_display_connectors[connector_id] =
        display_util->GetExternalDisplayInfo(connector_id);
    // If the connector info has missing fields, it is possible that DRM have
    // not fully detected all information yet. Retry to ensure that all DRM
    // changes are detected.
    if (times < kMaximumGetExternalDisplayInfoRetry &&
        HasMissingDrmField(external_display_connectors[connector_id])) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&GetConnectedExternalDisplayConnectorsHelper,
                         display_util_factory, last_known_connectors,
                         std::move(callback), times + 1),
          kGetExternalDisplayInfoRetryPeriod);
      return;
    }
  }

  std::move(callback).Run(std::move(external_display_connectors), std::nullopt);
}

}  // namespace

DelegateImpl::DelegateImpl(ec::EcCommandFactoryInterface* ec_command_factory,
                           DisplayUtilFactory* display_util_factory)
    : ec_command_factory_(ec_command_factory),
      display_util_factory_(display_util_factory) {}

DelegateImpl::~DelegateImpl() = default;

void DelegateImpl::GetFingerprintFrame(mojom::FingerprintCaptureType type,
                                       GetFingerprintFrameCallback callback) {
  auto result = mojom::FingerprintFrameResult::New();
  auto cros_fd = base::ScopedFD(open(path::kCrosFpDevice, O_RDWR));

  std::unique_ptr<ec::FpInfoCommand> info =
      ec_command_factory_->FpInfoCommand();
  if (!info || !info->Run(cros_fd.get())) {
    std::move(callback).Run(std::move(result),
                            "Failed to run ec::FpInfoCommand");
    return;
  }

  result->width = info->sensor_image()->width;
  result->height = info->sensor_image()->height;

  std::unique_ptr<ec::MkbpEvent> mkbp_event =
      CreateMkbpEvent(cros_fd.get(), EC_MKBP_EVENT_FINGERPRINT);
  if (!mkbp_event || mkbp_event->Enable() != 0) {
    PLOG(ERROR) << "Failed to enable fingerprint event";
    std::move(callback).Run(std::move(result),
                            "Failed to enable fingerprint event");
    return;
  }

  std::unique_ptr<ec::FpModeCommand> fp_mode_cmd =
      ec_command_factory_->FpModeCommand(ToEcFpMode(type));
  if (!fp_mode_cmd || !fp_mode_cmd->Run(cros_fd.get())) {
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
  int rv = mkbp_event->Wait(5000);
  if (rv != 1) {
    PLOG(ERROR) << "Failed to poll fingerprint event after 5 seconds";
    std::move(callback).Run(std::move(result),
                            "Failed to poll fingerprint event after 5 seconds");
    return;
  }

  std::unique_ptr<ec::GetProtocolInfoCommand> ec_protocol_cmd =
      ec_command_factory_->GetProtocolInfoCommand();
  if (!ec_protocol_cmd ||
      !ec_protocol_cmd->RunWithMultipleAttempts(cros_fd.get(), 2)) {
    std::move(callback).Run(std::move(result),
                            "Failed to get EC protocol info");
    return;
  }

  uint32_t size = result->width * result->height;
  if (size == 0) {
    std::move(callback).Run(std::move(result), "Frame size is zero");
    return;
  }

  auto fp_frame_command = ec_command_factory_->FpFrameCommand(
      FP_FRAME_INDEX_RAW_IMAGE, size, ec_protocol_cmd->MaxReadBytes());
  if (!fp_frame_command || !fp_frame_command->Run(cros_fd.get())) {
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
  auto cros_fd = base::ScopedFD(open(path::kCrosFpDevice, O_RDWR));

  auto version = ec_command_factory_->GetVersionCommand();
  if (!version || !version->Run(cros_fd.get())) {
    std::move(callback).Run(std::move(result),
                            "Failed to get fingerprint version");
    return;
  }

  result->rw_fw = version->Image() == EC_IMAGE_RW;

  std::move(callback).Run(std::move(result), std::nullopt);
}

void DelegateImpl::SetLedColor(mojom::LedName name,
                               mojom::LedColor color,
                               SetLedColorCallback callback) {
  auto ec_led_id = ToEcLedId(name);
  if (ec_led_id == EC_LED_ID_COUNT) {
    std::move(callback).Run("Unknown LED name");
    return;
  }
  auto ec_led_color = ToEcLedColor(color);
  if (ec_led_color == EC_LED_COLOR_COUNT) {
    std::move(callback).Run("Unknown LED color");
    return;
  }

  auto cros_fd = base::ScopedFD(open(ec::kCrosEcPath, O_RDONLY));

  auto query_cmd = ec_command_factory_->LedControlQueryCommand(ec_led_id);
  if (!query_cmd || !query_cmd->Run(cros_fd.get())) {
    std::move(callback).Run("Failed to query the LED brightness range");
    return;
  }

  uint8_t max_brightness = query_cmd->BrightnessRange()[ec_led_color];
  if (max_brightness == 0) {
    std::move(callback).Run("Unsupported color");
    return;
  }

  std::array<uint8_t, EC_LED_COLOR_COUNT> brightness = {};
  brightness[ec_led_color] = max_brightness;

  auto set_cmd =
      ec_command_factory_->LedControlSetCommand(ec_led_id, brightness);
  if (!set_cmd || !set_cmd->Run(cros_fd.get())) {
    std::move(callback).Run("Failed to set the LED color");
    return;
  }

  std::move(callback).Run(std::nullopt);
}

void DelegateImpl::ResetLedColor(mojom::LedName name,
                                 ResetLedColorCallback callback) {
  auto ec_led_id = ToEcLedId(name);
  if (ec_led_id == EC_LED_ID_COUNT) {
    std::move(callback).Run("Unknown LED name");
    return;
  }

  auto cros_fd = base::ScopedFD(open(ec::kCrosEcPath, O_RDONLY));

  auto cmd = ec_command_factory_->LedControlAutoCommand(ec_led_id);
  if (!cmd || !cmd->Run(cros_fd.get())) {
    std::move(callback).Run("Failed to reset LED color");
    return;
  }

  std::move(callback).Run(std::nullopt);
}

void DelegateImpl::MonitorAudioJack(
    mojo::PendingRemote<mojom::AudioJackObserver> observer) {
  MonitorEvdevEvents(
      std::make_unique<AudioJackEvdevDelegate>(std::move(observer)),
      /*allow_multiple_devices=*/true);
}

void DelegateImpl::MonitorTouchpad(
    mojo::PendingRemote<mojom::TouchpadObserver> observer) {
  MonitorEvdevEvents(
      std::make_unique<TouchpadEvdevDelegate>(std::move(observer)));
}

void DelegateImpl::FetchBootPerformance(FetchBootPerformanceCallback callback) {
  std::move(callback).Run(FetchBootPerformanceInfo());
}

void DelegateImpl::MonitorTouchscreen(
    mojo::PendingRemote<mojom::TouchscreenObserver> observer) {
  MonitorEvdevEvents(
      std::make_unique<TouchscreenEvdevDelegate>(std::move(observer)));
}

void DelegateImpl::MonitorStylusGarage(
    mojo::PendingRemote<mojom::StylusGarageObserver> observer) {
  MonitorEvdevEvents(
      std::make_unique<StylusGarageEvdevDelegate>(std::move(observer)));
}

void DelegateImpl::MonitorStylus(
    mojo::PendingRemote<mojom::StylusObserver> observer) {
  MonitorEvdevEvents(
      std::make_unique<StylusEvdevDelegate>(std::move(observer)));
}

void DelegateImpl::GetLidAngle(GetLidAngleCallback callback) {
  auto cros_fd = base::ScopedFD(open(ec::kCrosEcPath, O_RDONLY));
  auto cmd = ec_command_factory_->MotionSenseCommandLidAngle();
  if (!cmd) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  if (!cmd->Run(cros_fd.get())) {
    // TODO(b/274524224): Remove the below invalid EC result handling.
    if (cmd->Result() == 1 || cmd->Result() == 3) {
      std::move(callback).Run(LID_ANGLE_UNRELIABLE);
      return;
    }
    std::move(callback).Run(std::nullopt);
    return;
  }
  std::move(callback).Run(cmd->LidAngle());
}

void DelegateImpl::GetPsr(GetPsrCallback callback) {
  PsrFetcher psr_fetcher;
  std::move(callback).Run(psr_fetcher.FetchPsrInfo());
}

void DelegateImpl::GetConnectedExternalDisplayConnectors(
    const std::optional<std::vector<uint32_t>>& last_known_connectors_const,
    GetConnectedExternalDisplayConnectorsCallback callback) {
  if (!last_known_connectors_const.has_value()) {
    GetConnectedExternalDisplayConnectorsHelper(
        display_util_factory_, std::nullopt, std::move(callback), 0);
    return;
  }

  std::vector<uint32_t> last_known_connectors = {};
  for (const auto& element : last_known_connectors_const.value()) {
    last_known_connectors.push_back(element);
  }
  std::sort(last_known_connectors.begin(), last_known_connectors.end());
  GetConnectedExternalDisplayConnectorsHelper(
      display_util_factory_, last_known_connectors, std::move(callback), 0);
}

void DelegateImpl::GetPrivacyScreenInfo(GetPrivacyScreenInfoCallback callback) {
  std::unique_ptr<DisplayUtil> display_util = display_util_factory_->Create();
  if (!display_util) {
    std::move(callback).Run(mojom::GetPrivacyScreenInfoResult::NewError(
        "Failed to create DisplayUtil"));
    return;
  }

  std::optional<uint32_t> connector_id =
      display_util->GetEmbeddedDisplayConnectorID();
  if (!connector_id.has_value()) {
    std::move(callback).Run(mojom::GetPrivacyScreenInfoResult::NewError(
        "Failed to find valid display"));
    return;
  }
  auto info = mojom::PrivacyScreenInfo::New();
  display_util->FillPrivacyScreenInfo(connector_id.value(),
                                      &info->privacy_screen_supported,
                                      &info->privacy_screen_enabled);

  std::move(callback).Run(
      mojom::GetPrivacyScreenInfoResult::NewInfo(std::move(info)));
}

void DelegateImpl::FetchDisplayInfo(FetchDisplayInfoCallback callback) {
  std::move(callback).Run(GetDisplayInfo(display_util_factory_));
}

void DelegateImpl::MonitorPowerButton(
    mojo::PendingRemote<mojom::PowerButtonObserver> observer) {
  MonitorEvdevEvents(
      std::make_unique<PowerButtonEvdevDelegate>(std::move(observer)),
      /*allow_multiple_devices=*/true);
}

void DelegateImpl::RunPrimeSearch(base::TimeDelta exec_duration,
                                  uint64_t max_num,
                                  RunPrimeSearchCallback callback) {
  std::move(callback).Run(RunCpuTaskRoutine(
      CreatePrimeNumberSearchDelegate(max_num), exec_duration));
}

void DelegateImpl::MonitorVolumeButton(
    mojo::PendingRemote<mojom::VolumeButtonObserver> observer) {
  MonitorEvdevEvents(
      std::make_unique<VolumeButtonEvdevDelegate>(std::move(observer)),
      /*allow_multiple_devices=*/true);
}

void DelegateImpl::RunFloatingPoint(base::TimeDelta exec_duration,
                                    RunFloatingPointCallback callback) {
  std::move(callback).Run(
      RunCpuTaskRoutine(CreateFloatingPointDelegate(), exec_duration));
}

void DelegateImpl::GetAllFanSpeed(GetAllFanSpeedCallback callback) {
  auto cros_fd = base::ScopedFD(open(ec::kCrosEcPath, O_RDONLY));
  std::vector<uint16_t> fan_rpms;
  std::optional<uint8_t> num_fans =
      GetNumFans(ec_command_factory_, cros_fd.get());

  if (!num_fans.has_value()) {
    std::move(callback).Run({}, "Failed to get number of fans");
    return;
  }

  for (uint8_t fan_idx = 0; fan_idx < num_fans.value(); ++fan_idx) {
    std::unique_ptr<ec::PwmGetFanTargetRpmCommand> get_fan_rpm =
        ec_command_factory_->PwmGetFanTargetRpmCommand(fan_idx);
    if (!get_fan_rpm || !get_fan_rpm->Run(cros_fd.get()) ||
        !get_fan_rpm->Rpm().has_value() ||
        get_fan_rpm->Rpm().value() == EC_FAN_SPEED_NOT_PRESENT) {
      LOG(ERROR) << "Failed to read fan speed for fan idx: "
                 << static_cast<int>(fan_idx);
      std::move(callback).Run({}, "Failed to read fan speed");
      return;
    }

    if (get_fan_rpm->Rpm().value() == EC_FAN_SPEED_STALLED_DEPRECATED) {
      // For a stalled fan, we will output the fan speed as 0.
      fan_rpms.push_back(0);
      continue;
    }

    fan_rpms.push_back(get_fan_rpm->Rpm().value());
  }

  std::move(callback).Run(fan_rpms, std::nullopt);
}

void DelegateImpl::SetFanSpeed(
    const base::flat_map<uint8_t, uint16_t>& fan_id_to_rpm,
    SetFanSpeedCallback callback) {
  auto cros_fd = base::ScopedFD(open(ec::kCrosEcPath, O_RDONLY));
  std::optional<uint8_t> num_fans =
      GetNumFans(ec_command_factory_, cros_fd.get());

  if (!num_fans.has_value()) {
    std::move(callback).Run("Failed to get number of fans");
    return;
  }

  for (const auto& [id, rpm] : fan_id_to_rpm) {
    if (id >= num_fans) {
      LOG(ERROR) << "Attempting to set fan speed on invalid fan id";
      continue;
    }
    ec::PwmSetFanTargetRpmCommand set_fan_rpm{rpm, id};
    if (!set_fan_rpm.Run(cros_fd.get())) {
      LOG(ERROR) << "Failed to set fan speed: " << static_cast<int>(rpm)
                 << " for fan idx: " << static_cast<int>(id);
      std::move(callback).Run("Failed to set fan speed");
      return;
    }
  }

  std::move(callback).Run(std::nullopt);
}

void DelegateImpl::SetAllFanAutoControl(SetAllFanAutoControlCallback callback) {
  auto cros_fd = base::ScopedFD(open(ec::kCrosEcPath, O_RDONLY));
  std::optional<uint8_t> num_fans =
      GetNumFans(ec_command_factory_, cros_fd.get());

  if (!num_fans.has_value()) {
    std::move(callback).Run("Failed to get number of fans");
    return;
  }

  for (uint8_t fan_idx = 0; fan_idx < num_fans.value(); ++fan_idx) {
    ec::ThermalAutoFanCtrlCommand set_auto_fan_ctrl{fan_idx};
    if (!set_auto_fan_ctrl.Run(cros_fd.get())) {
      LOG(ERROR) << "Failed to set fan speed to auto control for fan idx: "
                 << static_cast<int>(fan_idx);
      // We should attempt to set all the fan to autocontrol, so even if one of
      // the fan fails, we should continue issuing command to others.
      continue;
    }
  }
  std::move(callback).Run(std::nullopt);
}

void DelegateImpl::GetEcThermalSensors(GetEcThermalSensorsCallback callback) {
  std::move(callback).Run(FetchEcThermalSensors());
}

void DelegateImpl::GetTouchpadDevices(GetTouchpadDevicesCallback callback) {
  std::unique_ptr<brillo::Udev> udev = brillo::Udev::Create();

  if (udev == nullptr) {
    std::move(callback).Run({}, "Error initializing udev");
    return;
  }

  auto result = PopulateTouchpadDevices(std::move(udev), "/");
  if (!result.has_value()) {
    std::move(callback).Run({}, result.error());
    return;
  }
  std::move(callback).Run(std::move(result.value()), std::nullopt);
}

void DelegateImpl::GetSmartBatteryManufactureDate(
    uint8_t i2c_port, GetSmartBatteryManufactureDateCallback callback) {
  auto cros_fd = base::ScopedFD(open(ec::kCrosEcPath, O_RDONLY));
  // Offset of manufacture date is 0x1B and the number of reading bytes is 2.
  auto cmd = ec_command_factory_->I2cReadCommand(
      /*port=*/i2c_port, /*addr8=*/kBatteryI2cAddress,
      /*offset=*/0x1B, /*read_len=*/2);
  if (!cmd || !cmd->Run(cros_fd.get())) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  std::move(callback).Run(cmd->Data());
}

void DelegateImpl::GetSmartBatteryTemperature(
    uint8_t i2c_port, GetSmartBatteryTemperatureCallback callback) {
  auto cros_fd = base::ScopedFD(open(ec::kCrosEcPath, O_RDONLY));
  // Offset of temperature is 0x08 and the number of reading bytes is 2.
  auto cmd = ec_command_factory_->I2cReadCommand(
      /*port=*/i2c_port, /*addr8=*/kBatteryI2cAddress,
      /*offset=*/0x08, /*read_len=*/2);
  if (!cmd || !cmd->Run(cros_fd.get())) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  std::move(callback).Run(cmd->Data());
}

void DelegateImpl::RunUrandom(base::TimeDelta exec_duration,
                              RunUrandomCallback callback) {
  std::move(callback).Run(
      RunCpuTaskRoutine(CreateUrandomDelegate(), exec_duration));
}

void DelegateImpl::RunNetworkBandwidthTest(
    mojom::NetworkBandwidthTestType type,
    const std::string& oem_name,
    mojo::PendingRemote<mojom::NetworkBandwidthObserver> observer,
    RunNetworkBandwidthTestCallback callback) {
  // There is no issue with leaking the thread pointer because the process will
  // be terminated after the posted task is finished.
  auto ndt_thread = new base::Thread("healthd_delegate_ndt_thread");
  CHECK(ndt_thread->Start()) << "Failed to start ndt thread.";
  ndt_thread->task_runner()->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&RunNdtTest, type, oem_name, std::move(observer)),
      std::move(callback));
}

void DelegateImpl::FetchGraphicsInfo(FetchGraphicsInfoCallback callback) {
  std::move(callback).Run(GetGraphicsInfo());
}

std::unique_ptr<ec::MkbpEvent> DelegateImpl::CreateMkbpEvent(
    int fd, enum ec_mkbp_event event_type) {
  return std::make_unique<ec::MkbpEvent>(fd, event_type);
}

std::unique_ptr<CpuRoutineTaskDelegate>
DelegateImpl::CreatePrimeNumberSearchDelegate(uint64_t max_num) {
  return std::make_unique<PrimeNumberSearchDelegateImpl>(max_num);
}

std::unique_ptr<CpuRoutineTaskDelegate>
DelegateImpl::CreateFloatingPointDelegate() {
  return std::make_unique<FloatingPointAccuracyDelegate>();
}

std::unique_ptr<CpuRoutineTaskDelegate> DelegateImpl::CreateUrandomDelegate() {
  return UrandomDelegate::Create();
}

}  // namespace diagnostics
