// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_METRICS_UTILS_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_METRICS_UTILS_CONSTANTS_H_

namespace diagnostics {

// These values are used in UMA. Please sync the change to
// tools/metrics/histograms/metadata/chromeos/histograms.xml in the Chromium
// repo.
namespace metrics_name {

inline constexpr char kTelemetryResultBattery[] =
    "ChromeOS.Healthd.TelemetryResult.Battery";
inline constexpr char kTelemetryResultCpu[] =
    "ChromeOS.Healthd.TelemetryResult.Cpu";
inline constexpr char kTelemetryResultBlockDevice[] =
    "ChromeOS.Healthd.TelemetryResult.BlockDevice";
inline constexpr char kTelemetryResultTimezone[] =
    "ChromeOS.Healthd.TelemetryResult.Timezone";
inline constexpr char kTelemetryResultMemory[] =
    "ChromeOS.Healthd.TelemetryResult.Memory";
inline constexpr char kTelemetryResultBacklight[] =
    "ChromeOS.Healthd.TelemetryResult.Backlight";
inline constexpr char kTelemetryResultFan[] =
    "ChromeOS.Healthd.TelemetryResult.Fan";
inline constexpr char kTelemetryResultStatefulPartition[] =
    "ChromeOS.Healthd.TelemetryResult.StatefulPartition";
inline constexpr char kTelemetryResultBluetooth[] =
    "ChromeOS.Healthd.TelemetryResult.Bluetooth";
inline constexpr char kTelemetryResultSystem[] =
    "ChromeOS.Healthd.TelemetryResult.System";
inline constexpr char kTelemetryResultNetwork[] =
    "ChromeOS.Healthd.TelemetryResult.Network";
inline constexpr char kTelemetryResultAudio[] =
    "ChromeOS.Healthd.TelemetryResult.Audio";
inline constexpr char kTelemetryResultBootPerformance[] =
    "ChromeOS.Healthd.TelemetryResult.BootPerformance";
inline constexpr char kTelemetryResultBus[] =
    "ChromeOS.Healthd.TelemetryResult.Bus";
inline constexpr char kTelemetryResultTpm[] =
    "ChromeOS.Healthd.TelemetryResult.Tpm";
inline constexpr char kTelemetryResultNetworkInterface[] =
    "ChromeOS.Healthd.TelemetryResult.NetworkInterface";
inline constexpr char kTelemetryResultGraphics[] =
    "ChromeOS.Healthd.TelemetryResult.Graphics";
inline constexpr char kTelemetryResultDisplay[] =
    "ChromeOS.Healthd.TelemetryResult.Display";
inline constexpr char kTelemetryResultInput[] =
    "ChromeOS.Healthd.TelemetryResult.Input";
inline constexpr char kTelemetryResultAudioHardware[] =
    "ChromeOS.Healthd.TelemetryResult.AudioHardware";
inline constexpr char kTelemetryResultSensor[] =
    "ChromeOS.Healthd.TelemetryResult.Sensor";

}  // namespace metrics_name
}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_METRICS_UTILS_CONSTANTS_H_
