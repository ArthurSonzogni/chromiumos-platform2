// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_H_
#define DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <base/task/single_thread_task_runner.h>
#include <base/time/time.h>
#include <brillo/dbus/dbus_connection.h>
#include <brillo/process/process.h>
#include <brillo/process/process_reaper.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/unique_receiver_set.h>

#include "diagnostics/cros_healthd/executor/utils/delegate_process.h"
#include "diagnostics/cros_healthd/executor/utils/sandboxed_process.h"
#include "diagnostics/cros_healthd/mojom/executor.mojom.h"

namespace org::chromium {
class DlcServiceInterfaceProxyInterface;
}  // namespace org::chromium

namespace diagnostics {
class DlcManager;
class ProcessControl;
struct ServiceConfig;

// Production implementation of the mojom::Executor Mojo interface.
class Executor final : public ash::cros_healthd::mojom::Executor {
 public:
  Executor(const scoped_refptr<base::SingleThreadTaskRunner> mojo_task_runner,
           mojo::PendingReceiver<ash::cros_healthd::mojom::Executor> receiver,
           brillo::ProcessReaper* process_reaper,
           base::OnceClosure on_disconnect,
           const ServiceConfig& service_config);
  Executor(const Executor&) = delete;
  Executor& operator=(const Executor&) = delete;
  ~Executor() override;

  // ash::cros_healthd::mojom::Executor overrides:
  void ReadFile(File file_enum, ReadFileCallback callback) override;
  void ReadFilePart(File file_enum,
                    uint64_t begin,
                    std::optional<uint64_t> size,
                    ReadFilePartCallback callback) override;
  void GetFileInfo(File file_enum, GetFileInfoCallback callback) override;
  void GetAllFanSpeed(GetAllFanSpeedCallback callback) override;
  void RunIw(IwCommand cmd,
             const std::string& interface_name,
             RunIwCallback callback) override;
  void RunMemtester(
      uint32_t test_mem_kib,
      mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl> receiver)
      override;
  void GetProcessIOContents(const std::vector<uint32_t>& pids,
                            GetProcessIOContentsCallback callback) override;
  void ReadMsr(const uint32_t msr_reg,
               uint32_t cpu_index,
               ReadMsrCallback callback) override;
  void GetLidAngle(GetLidAngleCallback callback) override;
  void GetFingerprintFrame(
      ash::cros_healthd::mojom::FingerprintCaptureType type,
      GetFingerprintFrameCallback callback) override;
  void GetFingerprintInfo(GetFingerprintInfoCallback callback) override;
  void SetLedColor(ash::cros_healthd::mojom::LedName name,
                   ash::cros_healthd::mojom::LedColor color,
                   SetLedColorCallback callback) override;
  void ResetLedColor(ash::cros_healthd::mojom::LedName name,
                     ResetLedColorCallback callback) override;
  void GetHciDeviceConfig(int32_t hci_interface,
                          GetHciDeviceConfigCallback callback) override;
  void MonitorAudioJack(
      mojo::PendingRemote<ash::cros_healthd::mojom::AudioJackObserver> observer,
      mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
          process_control_receiver) override;
  void MonitorTouchpad(
      mojo::PendingRemote<ash::cros_healthd::mojom::TouchpadObserver> observer,
      mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
          process_control_receiver) override;
  void FetchBootPerformance(FetchBootPerformanceCallback callback) override;
  void MonitorTouchscreen(
      mojo::PendingRemote<ash::cros_healthd::mojom::TouchscreenObserver>
          observer,
      mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
          process_control_receiver) override;
  void MonitorStylusGarage(
      mojo::PendingRemote<ash::cros_healthd::mojom::StylusGarageObserver>
          observer,
      mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
          process_control_receiver) override;
  void MonitorStylus(
      mojo::PendingRemote<ash::cros_healthd::mojom::StylusObserver> observer,
      mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
          process_control_receiver) override;
  void GetPsr(GetPsrCallback callback) override;
  void FetchCrashFromCrashSender(
      FetchCrashFromCrashSenderCallback callback) override;
  void RunStressAppTest(
      uint32_t test_mem_mib,
      uint32_t test_seconds,
      ash::cros_healthd::mojom::StressAppTestType test_type,
      mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl> receiver)
      override;
  void RunFio(ash::cros_healthd::mojom::FioJobArgumentPtr argument,
              mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
                  receiver) override;
  void RemoveFioTestFile(RemoveFioTestFileCallback callback) override;
  void GetConnectedExternalDisplayConnectors(
      const std::optional<std::vector<uint32_t>>& last_known_connectors,
      GetConnectedExternalDisplayConnectorsCallback callback) override;
  void GetPrivacyScreenInfo(GetPrivacyScreenInfoCallback callback) override;
  void FetchDisplayInfo(FetchDisplayInfoCallback callback) override;
  void MonitorPowerButton(
      mojo::PendingRemote<ash::cros_healthd::mojom::PowerButtonObserver>
          observer,
      mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
          process_control_receiver) override;
  void RunPrimeSearch(
      base::TimeDelta exec_duration,
      uint64_t max_num,
      mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
          process_control_receiver,
      RunPrimeSearchCallback callback) override;
  void MonitorVolumeButton(
      mojo::PendingRemote<ash::cros_healthd::mojom::VolumeButtonObserver>
          observer,
      mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
          process_control_receiver) override;
  void RunFloatingPoint(
      base::TimeDelta exec_duration,
      mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
          process_control_receiver,
      RunFloatingPointCallback callback) override;
  void StartBtmon(
      int32_t hci_interface,
      mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl> receiver)
      override;
  void ReadBtmonLog(ReadBtmonLogCallback callback) override;
  void RemoveBtmonLog(RemoveBtmonLogCallback callback) override;
  void SetFanSpeed(const base::flat_map<uint8_t, uint16_t>& fan_id_to_rpm,
                   SetFanSpeedCallback callback) override;
  void SetAllFanAutoControl(SetAllFanAutoControlCallback callback) override;
  void GetEcThermalSensors(GetEcThermalSensorsCallback callback) override;
  void GetTouchpadDevices(GetTouchpadDevicesCallback callback) override;
  void GetSmartBatteryManufactureDate(
      uint8_t i2c_port,
      GetSmartBatteryManufactureDateCallback callback) override;
  void GetSmartBatteryTemperature(
      uint8_t i2c_port, GetSmartBatteryTemperatureCallback callback) override;
  void RunUrandom(
      base::TimeDelta exec_duration,
      mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
          process_control_receiver,
      RunUrandomCallback callback) override;
  void RunNetworkBandwidthTest(
      ash::cros_healthd::mojom::NetworkBandwidthTestType type,
      const std::string& oem_name,
      mojo::PendingRemote<ash::cros_healthd::mojom::NetworkBandwidthObserver>
          observer,
      mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
          process_control,
      RunNetworkBandwidthTestCallback callback) override;
  void FetchGraphicsInfo(FetchGraphicsInfoCallback callback) override;

 private:
  // Runs the given process and wait for it to die. Does not track the process
  // it launches, so the launched process cannot be cancelled once it is
  // started. If cancelling is required, RunLongRunningProcess() should be used
  // instead.
  void RunAndWaitProcess(
      std::unique_ptr<brillo::ProcessImpl> process,
      base::OnceCallback<
          void(ash::cros_healthd::mojom::ExecutedProcessResultPtr)> callback);
  void OnRunAndWaitProcessFinished(
      base::OnceCallback<
          void(ash::cros_healthd::mojom::ExecutedProcessResultPtr)> callback,
      std::unique_ptr<brillo::ProcessImpl> process,
      const siginfo_t& siginfo);

  // Runs a long running delegate process and uses process control to track the
  // delegate process.
  void RunLongRunningDelegate(
      std::unique_ptr<DelegateProcess> delegate,
      mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl> receiver);
  // Runs a long running process and uses process control to track binary.
  void RunLongRunningProcess(
      std::unique_ptr<SandboxedProcess> process,
      mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl> receiver,
      bool combine_stdout_and_stderr);

  // Run fio after getting the DLC root path.
  void RunFioWithDlcRoot(
      ash::cros_healthd::mojom::FioJobArgumentPtr argument,
      mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl> receiver,
      std::optional<base::FilePath> dlc_root_path);

  // Create a |SandboxedProcess| instance.
  std::unique_ptr<SandboxedProcess> CreateProcess(
      const std::vector<std::string>& command,
      std::string_view seccomp_filename,
      const SandboxedProcess::Options& options) const;

  // Create a |DelegateProcess| instance.
  std::unique_ptr<DelegateProcess> CreateDelegateProcess(
      std::string_view seccomp_filename,
      const SandboxedProcess::Options& options) const;

  // Task runner for all Mojo callbacks.
  const scoped_refptr<base::SingleThreadTaskRunner> mojo_task_runner_;

  // Provides a Mojo endpoint that cros_healthd can call to access the
  // executor's Mojo methods.
  mojo::Receiver<ash::cros_healthd::mojom::Executor> receiver_;

  // Used to hold the child process and receiver. So the remote can reset the
  // mojo connection to terminate the child process.
  mojo::UniqueReceiverSet<ash::cros_healthd::mojom::ProcessControl>
      process_control_set_;

  // Used to monitor child process status.
  brillo::ProcessReaper* process_reaper_;

  // This should be the only connection to D-Bus. Use |connection_| to get the
  // |dbus_bus|.
  brillo::DBusConnection connection_;

  // Used to access DLC state and install DLC.
  std::unique_ptr<org::chromium::DlcServiceInterfaceProxyInterface>
      dlcservice_proxy_;
  std::unique_ptr<DlcManager> dlc_manager_;

  // Whether to override the sandboxing option when creating the processes.
  bool skip_sandbox_ = false;

  // Whether to enable landlock protection when creating the processes.
  bool enable_pending_landlock_ = false;

  // Must be the last member of the class.
  base::WeakPtrFactory<Executor> weak_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_H_
