// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/delegate_impl.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fcntl.h>
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
#include <libec/led_control_command.h>
#include <libec/mkbp_event.h>
#include <libec/motion_sense_command_lid_angle.h>
#include <libec/pwm/pwm_get_fan_target_rpm_command.h>
#include <libec/pwm/pwm_set_fan_target_rpm_command.h>
#include <libec/thermal/get_memmap_temp_b_command.h>
#include <libec/thermal/get_memmap_temp_command.h>
#include <libec/thermal/get_memmap_thermal_version_command.h>
#include <libec/thermal/temp_sensor_get_info_command.h>
#include <libec/thermal/thermal_auto_fan_ctrl_command.h>

#include "diagnostics/cros_healthd/delegate/constants.h"
#include "diagnostics/cros_healthd/delegate/fetchers/boot_performance.h"
#include "diagnostics/cros_healthd/delegate/fetchers/display_fetcher.h"
#include "diagnostics/cros_healthd/delegate/fetchers/touchpad_fetcher.h"
#include "diagnostics/cros_healthd/delegate/routines/floating_point_accuracy.h"
#include "diagnostics/cros_healthd/delegate/routines/prime_number_search.h"
#include "diagnostics/cros_healthd/delegate/utils/display_utils.h"
#include "diagnostics/cros_healthd/delegate/utils/evdev_utils.h"
#include "diagnostics/cros_healthd/delegate/utils/psr_cmd.h"
#include "diagnostics/cros_healthd/executor/constants.h"
#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

// The maximum number of times we will retry getting external display info.
constexpr int kMaximumGetExternalDisplayInfoRetry = 10;

// The interval to wait between retrying to get external display info.
constexpr base::TimeDelta kGetExternalDisplayInfoRetryPeriod =
    base::Milliseconds(500);

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

mojom::PsrInfo::LogState LogStateToMojo(diagnostics::psr::LogState log_state) {
  switch (log_state) {
    case diagnostics::psr::LogState::kNotStarted:
      return mojom::PsrInfo::LogState::kNotStarted;
    case diagnostics::psr::LogState::kStarted:
      return mojom::PsrInfo::LogState::kStarted;
    case diagnostics::psr::LogState::kStopped:
      return mojom::PsrInfo::LogState::kStopped;
  }
}

// A common util function to read the number of fans in the device.
std::optional<uint8_t> GetNumFans(const int cros_fd) {
  ec::GetFeaturesCommand get_features;
  if (!get_features.Run(cros_fd)) {
    LOG(ERROR) << "Failed to run ec::GetFeaturesCommand";
    return std::nullopt;
  }

  if (!get_features.IsFeatureSupported(EC_FEATURE_PWM_FAN)) {
    return 0;
  }

  static_assert(EC_FAN_SPEED_ENTRIES < std::numeric_limits<uint8_t>::max(),
                "Value of EC_FAN_SPEED_ENTRIES exceeds maximum value of uint8");

  uint8_t fan_idx;
  for (fan_idx = 0; fan_idx < EC_FAN_SPEED_ENTRIES; ++fan_idx) {
    ec::PwmGetFanTargetRpmCommand get_fan_rpm{fan_idx};
    if (!get_fan_rpm.Run(cros_fd) || !get_fan_rpm.Rpm().has_value()) {
      LOG(ERROR) << "Failed to read fan speed for fan idx: "
                 << static_cast<int>(fan_idx);
      return std::nullopt;
    }

    if (get_fan_rpm.Rpm().value() == EC_FAN_SPEED_NOT_PRESENT)
      return fan_idx;
  }
  return fan_idx;
}

double KelvinToCelsius(int temperature_kelvin) {
  return static_cast<double>(temperature_kelvin) - 273.15;
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

DelegateImpl::DelegateImpl(ec::EcCommandFactoryInterface* ec_command_factory)
    : ec_command_factory_(ec_command_factory) {}

DelegateImpl::~DelegateImpl() = default;

void DelegateImpl::GetFingerprintFrame(mojom::FingerprintCaptureType type,
                                       GetFingerprintFrameCallback callback) {
  auto result = mojom::FingerprintFrameResult::New();
  auto cros_fd = base::ScopedFD(open(path::kCrosFpDevice, O_RDWR));

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
  auto cros_fd = base::ScopedFD(open(path::kCrosFpDevice, O_RDWR));

  ec::GetVersionCommand version;
  if (!version.Run(cros_fd.get())) {
    std::move(callback).Run(std::move(result),
                            "Failed to get fingerprint version");
    return;
  }

  result->rw_fw = version.Image() == EC_IMAGE_RW;

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

  auto cros_fd = base::ScopedFD(open(ec::kCrosEcPath, O_RDWR));

  ec::LedControlQueryCommand query_cmd(ec_led_id);
  if (!query_cmd.Run(cros_fd.get())) {
    std::move(callback).Run("Failed to query the LED brightness range");
    return;
  }

  uint8_t max_brightness = query_cmd.BrightnessRange()[ec_led_color];
  if (max_brightness == 0) {
    std::move(callback).Run("Unsupported color");
    return;
  }

  std::array<uint8_t, EC_LED_COLOR_COUNT> brightness = {};
  brightness[ec_led_color] = max_brightness;

  ec::LedControlSetCommand set_cmd(ec_led_id, brightness);
  if (!set_cmd.Run(cros_fd.get())) {
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

  auto cros_fd = base::ScopedFD(open(ec::kCrosEcPath, O_RDWR));

  auto cmd = ec_command_factory_->LedControlAutoCommand(ec_led_id);
  if (!cmd || !cmd->Run(cros_fd.get())) {
    std::move(callback).Run("Failed to reset LED color");
    return;
  }

  std::move(callback).Run(std::nullopt);
}

void DelegateImpl::MonitorAudioJack(
    mojo::PendingRemote<mojom::AudioJackObserver> observer) {
  auto delegate = std::make_unique<EvdevAudioJackObserver>(std::move(observer));
  // Long-run method. The following object keeps alive until the process
  // terminates.
  new EvdevUtil(std::move(delegate), /*allow_multiple_devices*/ true);
}

void DelegateImpl::MonitorTouchpad(
    mojo::PendingRemote<mojom::TouchpadObserver> observer) {
  auto delegate = std::make_unique<EvdevTouchpadObserver>(std::move(observer));
  // Long-run method. The following object keeps alive until the process
  // terminates.
  new EvdevUtil(std::move(delegate));
}

void DelegateImpl::FetchBootPerformance(FetchBootPerformanceCallback callback) {
  std::move(callback).Run(FetchBootPerformanceInfo());
}

void DelegateImpl::MonitorTouchscreen(
    mojo::PendingRemote<mojom::TouchscreenObserver> observer) {
  auto delegate =
      std::make_unique<EvdevTouchscreenObserver>(std::move(observer));
  // Long-run method. The following object keeps alive until the process
  // terminates.
  new EvdevUtil(std::move(delegate));
}

void DelegateImpl::MonitorStylusGarage(
    mojo::PendingRemote<mojom::StylusGarageObserver> observer) {
  auto delegate =
      std::make_unique<EvdevStylusGarageObserver>(std::move(observer));
  // Long-run method. The following object keeps alive until the process
  // terminates.
  new EvdevUtil(std::move(delegate));
}

void DelegateImpl::MonitorStylus(
    mojo::PendingRemote<mojom::StylusObserver> observer) {
  auto delegate = std::make_unique<EvdevStylusObserver>(std::move(observer));
  // Long-run method. The following object keeps alive until the process
  // terminates.
  new EvdevUtil(std::move(delegate));
}

void DelegateImpl::GetLidAngle(GetLidAngleCallback callback) {
  auto cros_fd = base::ScopedFD(open(ec::kCrosEcPath, O_RDWR));
  ec::MotionSenseCommandLidAngle cmd;
  if (!cmd.Run(cros_fd.get())) {
    // TODO(b/274524224): Remove the below invalid EC result handling.
    if (cmd.Result() == 1 || cmd.Result() == 3) {
      std::move(callback).Run(LID_ANGLE_UNRELIABLE);
      return;
    }
    std::move(callback).Run(std::nullopt);
    return;
  }
  std::move(callback).Run(cmd.LidAngle());
}

void DelegateImpl::GetPsr(GetPsrCallback callback) {
  auto result = mojom::PsrInfo::New();

  // Treat a device that doesn't have /dev/mei0 as not supporting PSR.
  if (!base::PathExists(base::FilePath(psr::kCrosMeiPath))) {
    std::move(callback).Run(std::move(result), std::nullopt);
    return;
  }

  auto psr_cmd = psr::PsrCmd(psr::kCrosMeiPath);
  if (std::optional<bool> check_psr_result =
          psr_cmd.CheckPlatformServiceRecord();
      !check_psr_result.has_value()) {
    std::move(callback).Run(std::move(result), "Check PSR is not working.");
    return;
  } else if (!check_psr_result.value()) {
    // PSR is not supported.
    std::move(callback).Run(std::move(result), std::nullopt);
    return;
  }

  psr::PsrHeciResp psr_res;
  result->is_supported = true;
  if (!psr_cmd.GetPlatformServiceRecord(psr_res)) {
    std::move(callback).Run(std::move(result), "Get PSR is not working.");
    return;
  }

  if (psr_res.psr_version.major != psr::kPsrVersionMajor ||
      psr_res.psr_version.minor != psr::kPsrVersionMinor) {
    std::move(callback).Run(std::move(result), "Requires PSR 2.0 version.");
    return;
  }

  result->log_state = LogStateToMojo(psr_res.log_state);
  result->uuid =
      psr_cmd.IdToHexString(psr_res.psr_record.uuid, psr::kUuidLength);
  result->upid =
      psr_cmd.IdToHexString(psr_res.psr_record.upid, psr::kUpidLength);
  result->log_start_date = psr_res.psr_record.genesis_info.genesis_date;
  result->oem_name =
      reinterpret_cast<char*>(psr_res.psr_record.genesis_info.oem_info);
  result->oem_make =
      reinterpret_cast<char*>(psr_res.psr_record.genesis_info.oem_make_info);
  result->oem_model =
      reinterpret_cast<char*>(psr_res.psr_record.genesis_info.oem_model_info);
  result->manufacture_country = reinterpret_cast<char*>(
      psr_res.psr_record.genesis_info.manufacture_country);
  result->oem_data =
      reinterpret_cast<char*>(psr_res.psr_record.genesis_info.oem_data);
  result->uptime_seconds =
      psr_res.psr_record.ledger_info
          .ledger_counter[psr::LedgerCounterIndex::kS0Seconds];
  result->s5_counter = psr_res.psr_record.ledger_info
                           .ledger_counter[psr::LedgerCounterIndex::kS0ToS5];
  result->s4_counter = psr_res.psr_record.ledger_info
                           .ledger_counter[psr::LedgerCounterIndex::kS0ToS4];
  result->s3_counter = psr_res.psr_record.ledger_info
                           .ledger_counter[psr::LedgerCounterIndex::kS0ToS3];
  result->warm_reset_counter =
      psr_res.psr_record.ledger_info
          .ledger_counter[psr::LedgerCounterIndex::kWarmReset];

  for (int i = 0; i < psr_res.psr_record.events_count; ++i) {
    auto event = psr_res.psr_record.events_info[i];
    auto tmp_event = mojom::PsrEvent::New();

    switch (event.event_type) {
      case psr::EventType::kLogStart:
        tmp_event->type = mojom::PsrEvent::EventType::kLogStart;
        break;
      case psr::EventType::kLogEnd:
        tmp_event->type = mojom::PsrEvent::EventType::kLogEnd;
        break;
      case psr::EventType::kMissing:
        tmp_event->type = mojom::PsrEvent::EventType::kMissing;
        break;
      case psr::EventType::kInvalid:
        tmp_event->type = mojom::PsrEvent::EventType::kInvalid;
        break;
      case psr::EventType::kPrtcFailure:
        tmp_event->type = mojom::PsrEvent::EventType::kPrtcFailure;
        break;
      case psr::EventType::kCsmeRecovery:
        tmp_event->type = mojom::PsrEvent::EventType::kCsmeRecovery;
        break;
      case psr::EventType::kCsmeDamState:
        tmp_event->type = mojom::PsrEvent::EventType::kCsmeDamState;
        break;
      case psr::EventType::kCsmeUnlockState:
        tmp_event->type = mojom::PsrEvent::EventType::kCsmeUnlockState;
        break;
      case psr::EventType::kSvnIncrease:
        tmp_event->type = mojom::PsrEvent::EventType::kSvnIncrease;
        break;
      case psr::EventType::kFwVersionChanged:
        tmp_event->type = mojom::PsrEvent::EventType::kFwVersionChanged;
        break;
    }

    tmp_event->time = event.timestamp;
    tmp_event->data = event.data;
    result->events.push_back(std::move(tmp_event));
  }

  std::move(callback).Run(std::move(result), std::nullopt);
}

void DelegateImpl::GetConnectedExternalDisplayConnectors(
    const std::optional<std::vector<uint32_t>>& last_known_connectors_const,
    GetConnectedExternalDisplayConnectorsCallback callback) {
  if (!last_known_connectors_const.has_value()) {
    GetConnectedExternalDisplayConnectorsHelper(std::nullopt,
                                                std::move(callback), 0);
    return;
  }

  std::vector<uint32_t> last_known_connectors = {};
  for (const auto& element : last_known_connectors_const.value()) {
    last_known_connectors.push_back(element);
  }
  std::sort(last_known_connectors.begin(), last_known_connectors.end());
  GetConnectedExternalDisplayConnectorsHelper(last_known_connectors,
                                              std::move(callback), 0);
}

void DelegateImpl::GetConnectedExternalDisplayConnectorsHelper(
    std::optional<std::vector<uint32_t>> last_known_connectors,
    GetConnectedExternalDisplayConnectorsCallback callback,
    int times) {
  DisplayUtil display_util;

  if (!display_util.Initialize()) {
    std::move(callback).Run(
        base::flat_map<uint32_t, mojom::ExternalDisplayInfoPtr>{},
        "Failed to initialize DisplayUtil");
    return;
  }

  base::flat_map<uint32_t, mojom::ExternalDisplayInfoPtr>
      external_display_connectors;

  std::vector<uint32_t> connector_ids =
      display_util.GetExternalDisplayConnectorIDs();

  if (last_known_connectors.has_value()) {
    std::sort(connector_ids.begin(), connector_ids.end());
    // If the connected connectors are identical to the previous state, it is
    // possible that DRM have not detected the new display yet. Retry to ensure
    // that all DRM changes are detected.
    if (last_known_connectors == connector_ids &&
        times < kMaximumGetExternalDisplayInfoRetry) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(
              &DelegateImpl::GetConnectedExternalDisplayConnectorsHelper,
              weak_ptr_factory_.GetWeakPtr(), last_known_connectors,
              std::move(callback), times + 1),
          kGetExternalDisplayInfoRetryPeriod);
      return;
    }
  }

  for (const auto& connector_id : connector_ids) {
    external_display_connectors[connector_id] =
        display_util.GetExternalDisplayInfo(connector_id);
    // If the connector info has missing fields, it is possible that DRM have
    // not fully detected all information yet. Retry to ensure that all DRM
    // changes are detected.
    if (times < kMaximumGetExternalDisplayInfoRetry &&
        HasMissingDrmField(external_display_connectors[connector_id])) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(
              &DelegateImpl::GetConnectedExternalDisplayConnectorsHelper,
              weak_ptr_factory_.GetWeakPtr(), last_known_connectors,
              std::move(callback), times + 1),
          kGetExternalDisplayInfoRetryPeriod);
      return;
    }
  }

  std::move(callback).Run(std::move(external_display_connectors), std::nullopt);
}

void DelegateImpl::GetPrivacyScreenInfo(GetPrivacyScreenInfoCallback callback) {
  DisplayUtil display_util;
  if (!display_util.Initialize()) {
    std::move(callback).Run(false, false, "Failed to initialize DisplayUtil");
    return;
  }

  std::optional<uint32_t> connector_id =
      display_util.GetEmbeddedDisplayConnectorID();
  if (!connector_id.has_value()) {
    std::move(callback).Run(false, false, "Failed to find valid display");
    return;
  }
  bool supported, enabled;
  display_util.FillPrivacyScreenInfo(connector_id.value(), &supported,
                                     &enabled);

  std::move(callback).Run(supported, enabled, std::nullopt);
}

void DelegateImpl::FetchDisplayInfo(FetchDisplayInfoCallback callback) {
  std::move(callback).Run(GetDisplayInfo());
}

void DelegateImpl::MonitorPowerButton(
    mojo::PendingRemote<mojom::PowerButtonObserver> observer) {
  auto delegate =
      std::make_unique<EvdevPowerButtonObserver>(std::move(observer));
  // Long-run method. The following object keeps alive until the process
  // terminates.
  new EvdevUtil(std::move(delegate), /*allow_multiple_devices*/ true);
}

void DelegateImpl::RunPrimeSearch(base::TimeDelta exec_duration,
                                  uint64_t max_num,
                                  RunPrimeSearchCallback callback) {
  base::TimeTicks end_time = base::TimeTicks::Now() + exec_duration;
  max_num = std::clamp(max_num, static_cast<uint64_t>(2),
                       PrimeNumberSearchDelegate::kMaxPrimeNumber);

  auto prime_number_search =
      std::make_unique<diagnostics::PrimeNumberSearchDelegate>(max_num);

  while (base::TimeTicks::Now() < end_time) {
    if (!prime_number_search->Run()) {
      std::move(callback).Run(false);
      return;
    }
  }

  std::move(callback).Run(true);
}

void DelegateImpl::MonitorVolumeButton(
    mojo::PendingRemote<mojom::VolumeButtonObserver> observer) {
  auto delegate =
      std::make_unique<EvdevVolumeButtonObserver>(std::move(observer));
  // Long-run method. The following object keeps alive until the process
  // terminates.
  new EvdevUtil(std::move(delegate), /*allow_multiple_devices*/ true);
}

void DelegateImpl::RunFloatingPoint(base::TimeDelta exec_duration,
                                    RunFloatingPointCallback callback) {
  base::TimeTicks end_time = base::TimeTicks::Now() + exec_duration;

  auto floating_point_accuracy =
      std::make_unique<diagnostics::FloatingPointAccuracyDelegate>();

  while (base::TimeTicks::Now() < end_time) {
    if (!floating_point_accuracy->Run()) {
      std::move(callback).Run(false);
      return;
    }
  }
  std::move(callback).Run(true);
}

void DelegateImpl::GetAllFanSpeed(GetAllFanSpeedCallback callback) {
  auto cros_fd = base::ScopedFD(open(ec::kCrosEcPath, O_RDWR));
  std::vector<uint16_t> fan_rpms;
  std::optional<uint8_t> num_fans = GetNumFans(cros_fd.get());

  if (!num_fans.has_value()) {
    std::move(callback).Run({}, "Failed to get number of fans");
    return;
  }

  for (uint8_t fan_idx = 0; fan_idx < num_fans.value(); ++fan_idx) {
    ec::PwmGetFanTargetRpmCommand get_fan_rpm{fan_idx};
    if (!get_fan_rpm.Run(cros_fd.get()) || !get_fan_rpm.Rpm().has_value() ||
        get_fan_rpm.Rpm().value() == EC_FAN_SPEED_NOT_PRESENT) {
      LOG(ERROR) << "Failed to read fan speed for fan idx: "
                 << static_cast<int>(fan_idx);
      std::move(callback).Run({}, "Failed to read fan speed");
      return;
    }

    if (get_fan_rpm.Rpm().value() == EC_FAN_SPEED_STALLED_DEPRECATED) {
      // For a stalled fan, we will output the fan speed as 0.
      fan_rpms.push_back(0);
      continue;
    }

    fan_rpms.push_back(get_fan_rpm.Rpm().value());
  }

  std::move(callback).Run(fan_rpms, std::nullopt);
}

void DelegateImpl::SetFanSpeed(
    const base::flat_map<uint8_t, uint16_t>& fan_id_to_rpm,
    SetFanSpeedCallback callback) {
  auto cros_fd = base::ScopedFD(open(ec::kCrosEcPath, O_RDWR));
  std::optional<uint8_t> num_fans = GetNumFans(cros_fd.get());

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
  auto cros_fd = base::ScopedFD(open(ec::kCrosEcPath, O_RDWR));
  std::optional<uint8_t> num_fans = GetNumFans(cros_fd.get());

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
  auto cros_fd = base::ScopedFD(open(ec::kCrosEcPath, O_RDWR));

  std::vector<mojom::ThermalSensorInfoPtr> thermal_sensors;

  ec::GetMemmapThermalVersionCommand get_version{};
  if (!get_version.Run(cros_fd.get()) ||
      !get_version.ThermalVersion().has_value()) {
    LOG(ERROR) << "Failed to read thermal sensor version";
    std::move(callback).Run({}, "Failed to read thermal sensor version");
    return;
  }

  for (uint8_t sensor_idx = 0; sensor_idx < EC_MAX_TEMP_SENSOR_ENTRIES;
       ++sensor_idx) {
    int temperature_offset;

    if (sensor_idx < EC_TEMP_SENSOR_ENTRIES) {
      ec::GetMemmapTempCommand get_temp{sensor_idx};
      if (!get_temp.Run(cros_fd.get()) || !get_temp.Temp().has_value()) {
        LOG(ERROR) << "Failed to read temperature for thermal sensor idx: "
                   << static_cast<int>(sensor_idx);
        continue;
      }
      temperature_offset = get_temp.Temp().value();
    } else if (get_version.ThermalVersion() >= 2) {
      // If the sensor index is larger than or equal to EC_TEMP_SENSOR_ENTRIES,
      // the temperature should be read from the second bank, which is only
      // supported in thermal version >= 2.
      ec::GetMemmapTempBCommand get_temp{sensor_idx};
      if (!get_temp.Run(cros_fd.get()) || !get_temp.Temp().has_value()) {
        LOG(ERROR) << "Failed to read temperature for thermal sensor idx: "
                   << static_cast<int>(sensor_idx);
        continue;
      }
      temperature_offset = get_temp.Temp().value();
    } else {
      // The sensor index is located in the second bank, but EC does not support
      // reading from it. Break and return only results from the first bank.
      LOG(WARNING) << "EC does not support reading more thermal sensors";
      break;
    }

    // TODO(b/304654144): Some boards without temperature sensors return 0
    // instead of EC_TEMP_SENSOR_NOT_PRESENT. Treat 0 (-73.15°C) as indicator of
    // absent temperature sensor.
    if (temperature_offset == EC_TEMP_SENSOR_NOT_PRESENT ||
        temperature_offset == 0) {
      break;
    }
    if (temperature_offset == EC_TEMP_SENSOR_ERROR) {
      LOG(ERROR) << "Error in thermal sensor idx: "
                 << static_cast<int>(sensor_idx);
      continue;
    }
    if (temperature_offset == EC_TEMP_SENSOR_NOT_POWERED) {
      LOG(ERROR) << "Thermal sensor not powered, idx: "
                 << static_cast<int>(sensor_idx);
      continue;
    }
    if (temperature_offset == EC_TEMP_SENSOR_NOT_CALIBRATED) {
      LOG(ERROR) << "Thermal sensor not calibrated, idx: "
                 << static_cast<int>(sensor_idx);
      continue;
    }

    ec::TempSensorGetInfoCommand get_info{sensor_idx};
    if (!get_info.Run(cros_fd.get()) || !get_info.SensorName().has_value()) {
      LOG(ERROR) << "Failed to read sensor info for thermal sensor idx: "
                 << static_cast<int>(sensor_idx);
      continue;
    }

    auto sensor_info = mojom::ThermalSensorInfo::New();
    sensor_info->temperature_celsius =
        KelvinToCelsius(temperature_offset + EC_TEMP_SENSOR_OFFSET);
    sensor_info->name = get_info.SensorName().value();
    sensor_info->source = mojom::ThermalSensorInfo::ThermalSensorSource::kEc;

    thermal_sensors.push_back(std::move(sensor_info));
  }

  std::move(callback).Run(std::move(thermal_sensors), std::nullopt);
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
}  // namespace diagnostics
