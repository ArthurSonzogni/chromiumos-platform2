// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_health_tool/diag/diag.h"

#include <stdlib.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include <base/logging.h>
#include <base/run_loop.h>
#include <base/time/time.h>
#include <brillo/flag_helper.h>
#include <mojo/service_constants.h>

#include "diagnostics/cros_health_tool/diag/diag_actions.h"
#include "diagnostics/cros_health_tool/diag/diag_constants.h"
#include "diagnostics/cros_health_tool/diag/observers/routine_observer.h"
#include "diagnostics/cros_health_tool/mojo_util.h"
#include "diagnostics/cros_health_tool/output_util.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::ash::cros_healthd::mojom;

// Poll interval while waiting for a routine to finish.
constexpr base::TimeDelta kRoutinePollIntervalTimeDelta =
    base::Milliseconds(100);
// Maximum time we're willing to wait for a routine to finish.
constexpr base::TimeDelta kMaximumRoutineExecutionTimeDelta = base::Hours(1);

const struct {
  const char* readable_name;
  mojo_ipc::LedName name;
} kLedNameSwitches[] = {{"battery", mojo_ipc::LedName::kBattery},
                        {"power", mojo_ipc::LedName::kPower},
                        {"adapter", mojo_ipc::LedName::kAdapter},
                        {"left", mojo_ipc::LedName::kLeft},
                        {"right", mojo_ipc::LedName::kRight}};

const struct {
  const char* readable_color;
  mojo_ipc::LedColor color;
} kLedColorSwitches[] = {{"red", mojo_ipc::LedColor::kRed},
                         {"green", mojo_ipc::LedColor::kGreen},
                         {"blue", mojo_ipc::LedColor::kBlue},
                         {"yellow", mojo_ipc::LedColor::kYellow},
                         {"white", mojo_ipc::LedColor::kWhite},
                         {"amber", mojo_ipc::LedColor::kAmber}};

mojo_ipc::LedName LedNameFromString(const std::string& str) {
  for (const auto& item : kLedNameSwitches) {
    if (str == item.readable_name) {
      return item.name;
    }
  }
  return mojo_ipc::LedName::kUnmappedEnumField;
}

mojo_ipc::LedColor LedColorFromString(const std::string& str) {
  for (const auto& item : kLedColorSwitches) {
    if (str == item.readable_color) {
      return item.color;
    }
  }
  return mojo_ipc::LedColor::kUnmappedEnumField;
}

int RunV2Routine(mojo_ipc::RoutineArgumentPtr argument) {
  mojo::Remote<ash::cros_healthd::mojom::CrosHealthdRoutinesService>
      cros_healthd_routines_service_;
  RequestMojoServiceWithDisconnectHandler(
      chromeos::mojo_services::kCrosHealthdRoutines,
      cros_healthd_routines_service_);

  base::RunLoop run_loop;
  mojo::Remote<mojo_ipc::RoutineControl> routine_control;
  mojo::PendingReceiver<mojo_ipc::RoutineControl> pending_receiver =
      routine_control.BindNewPipeAndPassReceiver();
  routine_control.set_disconnect_with_reason_handler(
      base::BindOnce([](uint32_t error, const std::string& message) {
        base::Value::Dict output;
        SetJsonDictValue("error", error, &output);
        SetJsonDictValue("message", message, &output);
        std::cout << "\nError: " << std::endl;
        OutputJson(output);
      }).Then(run_loop.QuitClosure()));
  cros_healthd_routines_service_->CreateRoutine(std::move(argument),
                                                std::move(pending_receiver));
  RoutineObserver observer = RoutineObserver(run_loop.QuitClosure());
  routine_control->AddObserver(observer.BindNewPipdAndPassRemote());
  routine_control->Start();
  run_loop.Run();
  return EXIT_SUCCESS;
}

}  // namespace

int diag_main(int argc, char** argv) {
  DEFINE_bool(crosh_help, false, "Display help specific to crosh usage.");
  DEFINE_string(action, "",
                "Action to perform. Options are:\n\tget_routines - retrieve "
                "available routines.\n\trun_routine - run specified routine.");
  DEFINE_string(routine, "",
                "Diagnostic routine to run. For a list of available routines, "
                "run 'diag --action=get_routines'.");
  DEFINE_uint32(force_cancel_at_percent, std::numeric_limits<uint32_t>::max(),
                "If specified, will attempt to cancel the routine when its "
                "progress exceeds the flag's value.\nValid range: [0, 100]");

  // Flags for the urandom routine:
  DEFINE_uint32(urandom_length_seconds, 0,
                "Number of seconds to run the urandom routine for.");

  // Flag shared by the CPU stress, CPU cache, floating point accuracy and prime
  // search routines.
  DEFINE_uint32(cpu_stress_length_seconds, 0,
                "Number of seconds to run the {cpu_stress, cpu_cache, "
                "floating_point_accuracy, prime_search} routine for.");

  DEFINE_uint32(length_seconds, 10,
                "Number of seconds to run the routine for.");
  DEFINE_bool(ac_power_is_connected, true,
              "Whether or not the AC power routine expects the power supply to "
              "be connected.");
  DEFINE_string(
      expected_power_type, "",
      "Optional type of power supply expected for the AC power routine.");
  DEFINE_uint32(wear_level_threshold, 0,
                "Threshold number in percentage which routine examines "
                "wear level of NVMe against. If not specified, device "
                "threshold set in cros-config will be used instead.");
  DEFINE_bool(nvme_self_test_long, false,
              "Long-time period self-test of NVMe would be performed with "
              "this flag being set.");
  DEFINE_int32(file_size_mb, 1024,
               "Size (MB) of the test file for disk_read routine to pass.");
  DEFINE_string(disk_read_routine_type, "linear",
                "Disk read routine type for the disk_read routine. Options are:"
                "\n\tlinear - linear read.\n\trandom - random read.");
  DEFINE_uint32(maximum_discharge_percent_allowed, 100,
                "Upper bound for the battery discharge routine.");
  DEFINE_uint32(minimum_charge_percent_required, 0,
                "Lower bound for the battery charge routine.");
  DEFINE_uint32(percentage_used_threshold, 255,
                "Threshold number in percentage which routine examines "
                "percentage used against.");

  // Flag for the video conferencing routine.
  DEFINE_string(stun_server_hostname, "",
                "Optional custom STUN server hostname for the video "
                "conferencing routine.");

  // Flag for the privacy screen routine.
  DEFINE_string(set_privacy_screen, "on", "Privacy screen target state.");

  // Flags for the LED routine.
  DEFINE_string(led_name, "",
                "The target LED for the LED routine. Options are:"
                "\n\tbattery, power, adapter, left, right.");
  DEFINE_string(led_color, "",
                "The target color for the LED routine. Options are:"
                "\n\tred, green, blue, yellow, white, amber.");

  // Flag for the audio set volume/gain routine.
  DEFINE_uint64(node_id, 0, "Target node id.");
  DEFINE_uint32(volume, 100, "Target volume. [0-100]");
  DEFINE_uint32(gain, 100, "Target gain. [0-100]");
  DEFINE_bool(mute_on, true, "Mute audio output device or not.");

  // Flag for the Bluetooth pairing routine.
  DEFINE_string(
      peripheral_id, "",
      "ID of Bluetooth peripheral device for the Bluetooth pairing routine.");

  // Flag for the memory routine.
  DEFINE_uint32(max_testing_mem_kib, std::numeric_limits<uint32_t>::max(),
                "Number of kib to run the memory test for.");

  brillo::FlagHelper::Init(argc, argv, "diag - Device diagnostic tool.");

  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();

  if (FLAGS_crosh_help) {
    std::cout << "Usage: [list|routine]" << std::endl;
    return EXIT_SUCCESS;
  }

  if (FLAGS_action == "") {
    std::cout << "--action must be specified. Use --help for help on usage."
              << std::endl;
    return EXIT_FAILURE;
  }

  DiagActions actions{kRoutinePollIntervalTimeDelta,
                      kMaximumRoutineExecutionTimeDelta};

  if (FLAGS_action == "get_routines")
    return actions.ActionGetRoutines() ? EXIT_SUCCESS : EXIT_FAILURE;

  if (FLAGS_action == "run_routine") {
    // This is the switch case for routines in CrosHealthdRoutineService
    if (FLAGS_routine == "memory_v2") {
      auto argument = mojo_ipc::MemoryRoutineArgument::New();
      if (command_line->HasSwitch("max_testing_mem_kib")) {
        argument->max_testing_mem_kib = FLAGS_max_testing_mem_kib;
      }
      return RunV2Routine(
          mojo_ipc::RoutineArgument::NewMemory(std::move(argument)));
    }

    std::map<std::string, mojo_ipc::DiagnosticRoutineEnum>
        switch_to_diagnostic_routine;
    for (const auto& item : kDiagnosticRoutineSwitches)
      switch_to_diagnostic_routine[item.switch_name] = item.routine;
    auto itr = switch_to_diagnostic_routine.find(FLAGS_routine);
    if (itr == switch_to_diagnostic_routine.end()) {
      std::cout << "Unknown routine: " << FLAGS_routine << std::endl;
      return EXIT_FAILURE;
    }

    if (command_line->HasSwitch("force_cancel_at_percent"))
      actions.ForceCancelAtPercent(FLAGS_force_cancel_at_percent);

    bool routine_result;
    switch (itr->second) {
      case mojo_ipc::DiagnosticRoutineEnum::kBatteryCapacity:
        routine_result = actions.ActionRunBatteryCapacityRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kBatteryHealth:
        routine_result = actions.ActionRunBatteryHealthRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kUrandom:
        routine_result = actions.ActionRunUrandomRoutine(
            command_line->HasSwitch("urandom_length_seconds")
                ? std::optional<uint32_t>(FLAGS_urandom_length_seconds)
                : std::nullopt);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kSmartctlCheck:
      case mojo_ipc::DiagnosticRoutineEnum::kSmartctlCheckWithPercentageUsed:
        routine_result = actions.ActionRunSmartctlCheckRoutine(
            command_line->HasSwitch("percentage_used_threshold")
                ? std::optional<uint32_t>(FLAGS_percentage_used_threshold)
                : std::nullopt);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kAcPower:
        routine_result = actions.ActionRunAcPowerRoutine(
            FLAGS_ac_power_is_connected
                ? mojo_ipc::AcPowerStatusEnum::kConnected
                : mojo_ipc::AcPowerStatusEnum::kDisconnected,
            (command_line->HasSwitch("expected_power_type"))
                ? std::optional<std::string>{FLAGS_expected_power_type}
                : std::nullopt);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kCpuCache:
        routine_result = actions.ActionRunCpuCacheRoutine(
            command_line->HasSwitch("cpu_stress_length_seconds")
                ? std::optional<uint32_t>(FLAGS_cpu_stress_length_seconds)
                : std::nullopt);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kCpuStress:
        routine_result = actions.ActionRunCpuStressRoutine(
            command_line->HasSwitch("cpu_stress_length_seconds")
                ? std::optional<uint32_t>(FLAGS_cpu_stress_length_seconds)
                : std::nullopt);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kFloatingPointAccuracy:
        routine_result = actions.ActionRunFloatingPointAccuracyRoutine(
            command_line->HasSwitch("cpu_stress_length_seconds")
                ? std::optional<uint32_t>(FLAGS_cpu_stress_length_seconds)
                : std::nullopt);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kNvmeWearLevel:
        routine_result = actions.ActionRunNvmeWearLevelRoutine(
            command_line->HasSwitch("wear_level_threshold")
                ? std::optional<std::uint32_t>{FLAGS_wear_level_threshold}
                : std::nullopt);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kNvmeSelfTest:
        routine_result = actions.ActionRunNvmeSelfTestRoutine(
            FLAGS_nvme_self_test_long
                ? mojo_ipc::NvmeSelfTestTypeEnum::kLongSelfTest
                : mojo_ipc::NvmeSelfTestTypeEnum::kShortSelfTest);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kDiskRead:
        mojo_ipc::DiskReadRoutineTypeEnum type;
        if (FLAGS_disk_read_routine_type == "linear") {
          type = mojo_ipc::DiskReadRoutineTypeEnum::kLinearRead;
        } else if (FLAGS_disk_read_routine_type == "random") {
          type = mojo_ipc::DiskReadRoutineTypeEnum::kRandomRead;
        } else {
          std::cout << "Unknown disk_read_routine_type: "
                    << FLAGS_disk_read_routine_type << std::endl;
          return EXIT_FAILURE;
        }
        routine_result = actions.ActionRunDiskReadRoutine(
            type, FLAGS_length_seconds, FLAGS_file_size_mb);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kPrimeSearch:
        routine_result = actions.ActionRunPrimeSearchRoutine(
            command_line->HasSwitch("cpu_stress_length_seconds")
                ? std::optional<uint32_t>(FLAGS_cpu_stress_length_seconds)
                : std::nullopt);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kBatteryDischarge:
        routine_result = actions.ActionRunBatteryDischargeRoutine(
            FLAGS_length_seconds, FLAGS_maximum_discharge_percent_allowed);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kBatteryCharge:
        routine_result = actions.ActionRunBatteryChargeRoutine(
            FLAGS_length_seconds, FLAGS_minimum_charge_percent_required);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kLanConnectivity:
        routine_result = actions.ActionRunLanConnectivityRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kSignalStrength:
        routine_result = actions.ActionRunSignalStrengthRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kMemory:
        routine_result = actions.ActionRunMemoryRoutine(
            command_line->HasSwitch("max_testing_mem_kib")
                ? std::optional<uint32_t>(FLAGS_max_testing_mem_kib)
                : std::nullopt);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kGatewayCanBePinged:
        routine_result = actions.ActionRunGatewayCanBePingedRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kHasSecureWiFiConnection:
        routine_result = actions.ActionRunHasSecureWiFiConnectionRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kDnsResolverPresent:
        routine_result = actions.ActionRunDnsResolverPresentRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kDnsLatency:
        routine_result = actions.ActionRunDnsLatencyRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kDnsResolution:
        routine_result = actions.ActionRunDnsResolutionRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kCaptivePortal:
        routine_result = actions.ActionRunCaptivePortalRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kHttpFirewall:
        routine_result = actions.ActionRunHttpFirewallRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kHttpsFirewall:
        routine_result = actions.ActionRunHttpsFirewallRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kHttpsLatency:
        routine_result = actions.ActionRunHttpsLatencyRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kVideoConferencing:
        routine_result = actions.ActionRunVideoConferencingRoutine(
            (FLAGS_stun_server_hostname == "")
                ? std::nullopt
                : std::optional<std::string>{FLAGS_stun_server_hostname});
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kArcHttp:
        routine_result = actions.ActionRunArcHttpRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kArcPing:
        routine_result = actions.ActionRunArcPingRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kArcDnsResolution:
        routine_result = actions.ActionRunArcDnsResolutionRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kSensitiveSensor:
        routine_result = actions.ActionRunSensitiveSensorRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kFingerprint:
        routine_result = actions.ActionRunFingerprintRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kFingerprintAlive:
        routine_result = actions.ActionRunFingerprintAliveRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kPrivacyScreen:
        bool target_state;
        if (FLAGS_set_privacy_screen == "on") {
          target_state = true;
        } else if (FLAGS_set_privacy_screen == "off") {
          target_state = false;
        } else {
          std::cout << "Invalid privacy screen target state: "
                    << FLAGS_set_privacy_screen << ". Should be on/off."
                    << std::endl;
          return EXIT_FAILURE;
        }
        routine_result = actions.ActionRunPrivacyScreenRoutine(target_state);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kLedLitUp: {
        mojo_ipc::LedName name = LedNameFromString(FLAGS_led_name);
        if (name == mojo_ipc::LedName::kUnmappedEnumField) {
          std::cout << "Unknown led_name: " << FLAGS_led_name << std::endl;
          return EXIT_FAILURE;
        }
        mojo_ipc::LedColor color = LedColorFromString(FLAGS_led_color);
        if (color == mojo_ipc::LedColor::kUnmappedEnumField) {
          std::cout << "Unknown led_color: " << FLAGS_led_color << std::endl;
          return EXIT_FAILURE;
        }
        routine_result = actions.ActionRunLedRoutine(name, color);
      } break;
      case mojo_ipc::DiagnosticRoutineEnum::kEmmcLifetime:
        routine_result = actions.ActionRunEmmcLifetimeRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kAudioSetVolume:
        routine_result = actions.ActionRunAudioSetVolumeRoutine(
            FLAGS_node_id, FLAGS_volume, FLAGS_mute_on);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kAudioSetGain:
        routine_result =
            actions.ActionRunAudioSetGainRoutine(FLAGS_node_id, FLAGS_gain);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kBluetoothPower:
        routine_result = actions.ActionRunBluetoothPowerRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kBluetoothDiscovery:
        routine_result = actions.ActionRunBluetoothDiscoveryRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kBluetoothScanning:
        routine_result = actions.ActionRunBluetoothScanningRoutine(
            command_line->HasSwitch("length_seconds")
                ? std::optional<uint32_t>(FLAGS_length_seconds)
                : std::nullopt);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kBluetoothPairing:
        if (FLAGS_peripheral_id.empty()) {
          std::cout << "Invalid empty peripheral_id" << std::endl;
          return EXIT_FAILURE;
        }
        routine_result =
            actions.ActionRunBluetoothPairingRoutine(FLAGS_peripheral_id);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kUnknown:
        // Never map FLAGS_routine to kUnknown field.
        NOTREACHED();
        return EXIT_FAILURE;
    }

    return routine_result ? EXIT_SUCCESS : EXIT_FAILURE;
  }

  std::cout << "Unknown action: " << FLAGS_action << std::endl;
  return EXIT_FAILURE;
}

}  // namespace diagnostics
