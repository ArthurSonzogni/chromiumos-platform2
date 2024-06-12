// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_METRICS_UTILS_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_METRICS_UTILS_CONSTANTS_H_

namespace diagnostics {

namespace metrics_enum {

// These values are logged to UMA. Entries should not be renumbered and
// numeric values should never be reused. Please keep in sync with
// "CrosHealthdTelemetryResult" in tools/metrics/histograms/enums.xml in the
// Chromium repo.
enum class CrosHealthdTelemetryResult {
  kSuccess = 0,
  kError = 1,
  // A special enumerator that must share the highest enumerator value. This
  // value is required when calling |SendEnumToUMA|.
  kMaxValue = kError,
};

// These values are logged to UMA. Entries should not be renumbered and
// numeric values should never be reused. Please keep in sync with
// "CrosHealthdDiagnosticResult" in tools/metrics/histograms/enums.xml in the
// Chromium repo.
enum class CrosHealthdDiagnosticResult {
  kPassed = 0,
  kFailed = 1,
  kError = 2,
  kCancelled = 3,
  kFailedToStart = 4,
  kRemoved = 5,
  kUnsupported = 6,
  kNotRun = 7,
  // A special enumerator that must share the highest enumerator value. This
  // value is required when calling |SendEnumToUMA|.
  kMaxValue = kNotRun,
};

// These values are logged to UMA. Entries should not be renumbered and
// numeric values should never be reused. Please keep in sync with
// "CrosHealthdEventCategory" in tools/metrics/histograms/enums.xml in the
// Chromium repo.
enum class CrosHealthdEventCategory {
  kUnknown = 0,
  kUsb = 1,
  kThunderbolt = 2,
  kLid = 3,
  kBluetooth = 4,
  kPower = 5,
  kAudio = 6,
  kAudioJack = 7,
  kSdCard = 8,
  kNetwork = 9,
  kKeyboardDiagnostic = 10,
  kTouchpad = 11,
  kExternalDisplay = 12,
  kTouchscreen = 13,
  kStylusGarage = 14,
  kStylus = 15,
  kCrash = 16,
  // A special enumerator that must share the highest enumerator value. This
  // value is required when calling |SendEnumToUMA|.
  kMaxValue = kCrash,
};

// These values are logged to UMA. Entries should not be renumbered and
// numeric values should never be reused. Please keep in sync with
// "CrosHealthdRoutineCategory" in tools/metrics/histograms/enums.xml in the
// Chromium repo.
enum class CrosHealthdRoutineCategory {
  kUnknown = 0,
  kPrimeSearch = 1,
  kFloatingPoint = 2,
  kMemory = 3,
  kAudioDriver = 4,
  kCpuStress = 5,
  kUfsLifetime = 6,
  kDiskRead = 7,
  kCpuCache = 8,
  kVolumeButton = 9,
  kLedLitUp = 10,
  kBluetoothPower = 11,
  kBluetoothDiscovery = 12,
  kFan = 13,
  kBluetoothScanning = 14,
  kBluetoothPairing = 15,
  kCameraAvailability = 16,
  kUrandom = 17,
  kNetworkBandwidth = 18,
  kSensitiveSensor = 19,
  kCameraFrameAnalysis = 20,
  kBatteryDischarge = 21,
  // A special enumerator that must share the highest enumerator value. This
  // value is required when calling `SendEnumToUMA`.
  kMaxValue = kBatteryDischarge,
};

}  // namespace metrics_enum

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
inline constexpr char kTelemetryResultThermal[] =
    "ChromeOS.Healthd.TelemetryResult.Thermal";

inline constexpr char kDiagnosticResultBatteryCapacity[] =
    "ChromeOS.Healthd.DiagnosticResult.BatteryCapacity";
inline constexpr char kDiagnosticResultBatteryHealth[] =
    "ChromeOS.Healthd.DiagnosticResult.BatteryHealth";
inline constexpr char kDiagnosticResultUrandom[] =
    "ChromeOS.Healthd.DiagnosticResult.Urandom";
inline constexpr char kDiagnosticResultSmartctlCheck[] =
    "ChromeOS.Healthd.DiagnosticResult.SmartctlCheck";
inline constexpr char kDiagnosticResultAcPower[] =
    "ChromeOS.Healthd.DiagnosticResult.AcPower";
inline constexpr char kDiagnosticResultCpuCache[] =
    "ChromeOS.Healthd.DiagnosticResult.CpuCache";
inline constexpr char kDiagnosticResultCpuStress[] =
    "ChromeOS.Healthd.DiagnosticResult.CpuStress";
inline constexpr char kDiagnosticResultFloatingPointAccuracy[] =
    "ChromeOS.Healthd.DiagnosticResult.FloatingPointAccuracy";
inline constexpr char kDiagnosticResultNvmeWearLevel[] =
    "ChromeOS.Healthd.DiagnosticResult.NvmeWearLevel";
inline constexpr char kDiagnosticResultNvmeSelfTest[] =
    "ChromeOS.Healthd.DiagnosticResult.NvmeSelfTest";
inline constexpr char kDiagnosticResultDiskRead[] =
    "ChromeOS.Healthd.DiagnosticResult.DiskRead";
inline constexpr char kDiagnosticResultPrimeSearch[] =
    "ChromeOS.Healthd.DiagnosticResult.PrimeSearch";
inline constexpr char kDiagnosticResultBatteryDischarge[] =
    "ChromeOS.Healthd.DiagnosticResult.BatteryDischarge";
inline constexpr char kDiagnosticResultBatteryCharge[] =
    "ChromeOS.Healthd.DiagnosticResult.BatteryCharge";
inline constexpr char kDiagnosticResultMemory[] =
    "ChromeOS.Healthd.DiagnosticResult.Memory";
inline constexpr char kDiagnosticResultLanConnectivity[] =
    "ChromeOS.Healthd.DiagnosticResult.LanConnectivity";
inline constexpr char kDiagnosticResultSignalStrength[] =
    "ChromeOS.Healthd.DiagnosticResult.SignalStrength";
inline constexpr char kDiagnosticResultGatewayCanBePinged[] =
    "ChromeOS.Healthd.DiagnosticResult.GatewayCanBePinged";
inline constexpr char kDiagnosticResultHasSecureWiFiConnection[] =
    "ChromeOS.Healthd.DiagnosticResult.HasSecureWiFiConnection";
inline constexpr char kDiagnosticResultDnsResolverPresent[] =
    "ChromeOS.Healthd.DiagnosticResult.DnsResolverPresent";
inline constexpr char kDiagnosticResultDnsLatency[] =
    "ChromeOS.Healthd.DiagnosticResult.DnsLatency";
inline constexpr char kDiagnosticResultDnsResolution[] =
    "ChromeOS.Healthd.DiagnosticResult.DnsResolution";
inline constexpr char kDiagnosticResultCaptivePortal[] =
    "ChromeOS.Healthd.DiagnosticResult.CaptivePortal";
inline constexpr char kDiagnosticResultHttpFirewall[] =
    "ChromeOS.Healthd.DiagnosticResult.HttpFirewall";
inline constexpr char kDiagnosticResultHttpsFirewall[] =
    "ChromeOS.Healthd.DiagnosticResult.HttpsFirewall";
inline constexpr char kDiagnosticResultHttpsLatency[] =
    "ChromeOS.Healthd.DiagnosticResult.HttpsLatency";
inline constexpr char kDiagnosticResultVideoConferencing[] =
    "ChromeOS.Healthd.DiagnosticResult.VideoConferencing";
inline constexpr char kDiagnosticResultArcHttp[] =
    "ChromeOS.Healthd.DiagnosticResult.ArcHttp";
inline constexpr char kDiagnosticResultArcPing[] =
    "ChromeOS.Healthd.DiagnosticResult.ArcPing";
inline constexpr char kDiagnosticResultArcDnsResolution[] =
    "ChromeOS.Healthd.DiagnosticResult.ArcDnsResolution";
inline constexpr char kDiagnosticResultSensitiveSensor[] =
    "ChromeOS.Healthd.DiagnosticResult.SensitiveSensor";
inline constexpr char kDiagnosticResultFingerprint[] =
    "ChromeOS.Healthd.DiagnosticResult.Fingerprint";
inline constexpr char kDiagnosticResultFingerprintAlive[] =
    "ChromeOS.Healthd.DiagnosticResult.FingerprintAlive";
inline constexpr char kDiagnosticResultPrivacyScreen[] =
    "ChromeOS.Healthd.DiagnosticResult.PrivacyScreen";
inline constexpr char kDiagnosticResultLedLitUp[] =
    "ChromeOS.Healthd.DiagnosticResult.LedLitUp";
inline constexpr char kDiagnosticResultSmartctlCheckWithPercentageUsed[] =
    "ChromeOS.Healthd.DiagnosticResult.SmartctlCheckWithPercentageUsed";
inline constexpr char kDiagnosticResultEmmcLifetime[] =
    "ChromeOS.Healthd.DiagnosticResult.EmmcLifetime";
inline constexpr char kDiagnosticResultAudioSetVolume[] =
    "ChromeOS.Healthd.DiagnosticResult.AudioSetVolume";
inline constexpr char kDiagnosticResultAudioSetGain[] =
    "ChromeOS.Healthd.DiagnosticResult.AudioSetGain";
inline constexpr char kDiagnosticResultBluetoothPower[] =
    "ChromeOS.Healthd.DiagnosticResult.BluetoothPower";
inline constexpr char kDiagnosticResultBluetoothDiscovery[] =
    "ChromeOS.Healthd.DiagnosticResult.BluetoothDiscovery";
inline constexpr char kDiagnosticResultBluetoothScanning[] =
    "ChromeOS.Healthd.DiagnosticResult.BluetoothScanning";
inline constexpr char kDiagnosticResultBluetoothPairing[] =
    "ChromeOS.Healthd.DiagnosticResult.BluetoothPairing";
inline constexpr char kDiagnosticResultPowerButton[] =
    "ChromeOS.Healthd.DiagnosticResult.PowerButton";
inline constexpr char kDiagnosticResultAudioDriver[] =
    "ChromeOS.Healthd.DiagnosticResult.AudioDriver";
inline constexpr char kDiagnosticResultUfsLifetime[] =
    "ChromeOS.Healthd.DiagnosticResult.UfsLifetime";
inline constexpr char kDiagnosticResultFan[] =
    "ChromeOS.Healthd.DiagnosticResult.Fan";

inline constexpr char kEventSubscription[] =
    "ChromeOS.Healthd.EventSubscription";

inline constexpr char kRoutineCreation[] = "ChromeOS.Healthd.RoutineCreation";

}  // namespace metrics_name
}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_METRICS_UTILS_CONSTANTS_H_
