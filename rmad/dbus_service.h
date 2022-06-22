// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_DBUS_SERVICE_H_
#define RMAD_DBUS_SERVICE_H_

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/dbus/data_serialization.h>
#include <brillo/dbus/dbus_method_response.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/dbus/dbus_signal.h>
#include <dbus/bus.h>

#include "rmad/daemon_callback.h"
#include "rmad/rmad_interface.h"
#include "rmad/system/tpm_manager_client.h"
#include "rmad/utils/cros_config_utils.h"
#include "rmad/utils/crossystem_utils.h"

namespace brillo {
namespace dbus_utils {

void AppendValueToWriter(dbus::MessageWriter* writer,
                         const rmad::HardwareVerificationResult& value);
void AppendValueToWriter(dbus::MessageWriter* writer,
                         const rmad::CalibrationComponentStatus& value);
void AppendValueToWriter(dbus::MessageWriter* writer,
                         const rmad::ProvisionStatus& value);
void AppendValueToWriter(dbus::MessageWriter* writer,
                         const rmad::FinalizeStatus& value);
bool PopValueFromReader(dbus::MessageReader* reader,
                        rmad::HardwareVerificationResult* value);
bool PopValueFromReader(dbus::MessageReader* reader,
                        rmad::CalibrationComponentStatus* value);
bool PopValueFromReader(dbus::MessageReader* reader,
                        rmad::ProvisionStatus* value);
bool PopValueFromReader(dbus::MessageReader* reader,
                        rmad::FinalizeStatus* value);

}  // namespace dbus_utils
}  // namespace brillo

namespace rmad {

class DBusService : public brillo::DBusServiceDaemon {
 public:
  explicit DBusService(RmadInterface* rmad_interface);
  // Used to inject a mock bus.
  DBusService(const scoped_refptr<dbus::Bus>& bus,
              RmadInterface* rmad_interface,
              const base::FilePath& state_file_path,
              std::unique_ptr<TpmManagerClient> tpm_manager_client,
              std::unique_ptr<CrosConfigUtils> cros_config_utils,
              std::unique_ptr<CrosSystemUtils> crossystem_utils);
  DBusService(const DBusService&) = delete;
  DBusService& operator=(const DBusService&) = delete;

  ~DBusService() override = default;

  void SendErrorSignal(RmadErrorCode error);  // This is currently not used.
  void SendHardwareVerificationResultSignal(
      const HardwareVerificationResult& result);
  void SendUpdateRoFirmwareStatusSignal(UpdateRoFirmwareStatus status);
  void SendCalibrationOverallSignal(CalibrationOverallStatus status);
  void SendCalibrationProgressSignal(CalibrationComponentStatus status);
  void SendProvisionProgressSignal(const ProvisionStatus& status);
  void SendFinalizeProgressSignal(const FinalizeStatus& status);
  void SendHardwareWriteProtectionStateSignal(bool enabled);
  void SendPowerCableStateSignal(bool plugged_in);

  void SetTestMode() { test_mode_ = true; }

 protected:
  // brillo::DBusServiceDaemon overrides.
  int OnEventLoopStarted() override;
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

  // Provide callbacks to rmad_interface.
  void SetUpInterfaceCallbacks();
  scoped_refptr<DaemonCallback> CreateDaemonCallback() const;

 private:
  friend class DBusServiceTest;

  bool CheckRmaCriteria() const;
  bool SetUpInterface();

  template <typename... Types>
  using DBusMethodResponse = brillo::dbus_utils::DBusMethodResponse<Types...>;

  // Template for handling D-Bus methods with a request.
  template <typename RequestType, typename ReplyProtobufType>
  using HandlerFunction = void (RmadInterface::*)(
      const RequestType&,
      base::OnceCallback<void(const ReplyProtobufType&, bool)>);

  template <typename RequestType,
            typename ReplyProtobufType,
            DBusService::HandlerFunction<RequestType, ReplyProtobufType> func>
  void DelegateToInterface(
      std::unique_ptr<DBusMethodResponse<ReplyProtobufType>> response,
      const RequestType& request) {
    // Reply messages should always contain an error field.
    if (!is_rma_required_) {
      ReplyProtobufType reply;
      reply.set_error(RMAD_ERROR_RMA_NOT_REQUIRED);
      SendReply(std::move(response), reply, true);
    } else if (!SetUpInterface()) {
      ReplyProtobufType reply;
      reply.set_error(RMAD_ERROR_DAEMON_INITIALIZATION_FAILED);
      SendReply(std::move(response), reply, true);
      return;
    } else {
      (rmad_interface_->*func)(
          request, base::BindOnce(&DBusService::SendReply<ReplyProtobufType>,
                                  base::Unretained(this), std::move(response)));
    }
  }

  // Template for handling D-Bus methods without a request.
  template <typename ReplyProtobufType>
  using HandlerFunctionEmptyRequest = void (RmadInterface::*)(
      base::OnceCallback<void(const ReplyProtobufType&, bool)>);

  template <typename ReplyProtobufType,
            DBusService::HandlerFunctionEmptyRequest<ReplyProtobufType> func>
  void DelegateToInterface(
      std::unique_ptr<DBusMethodResponse<ReplyProtobufType>> response) {
    // Reply messages should always contain an error field.
    if (!is_rma_required_) {
      ReplyProtobufType reply;
      reply.set_error(RMAD_ERROR_RMA_NOT_REQUIRED);
      SendReply(std::move(response), reply, true);
    } else if (!SetUpInterface()) {
      ReplyProtobufType reply;
      reply.set_error(RMAD_ERROR_DAEMON_INITIALIZATION_FAILED);
      SendReply(std::move(response), reply, true);
    } else {
      (rmad_interface_->*func)(
          base::BindOnce(&DBusService::SendReply<ReplyProtobufType>,
                         base::Unretained(this), std::move(response)));
    }
  }

  void HandleIsRmaRequiredMethod(
      std::unique_ptr<DBusMethodResponse<bool>> response);

  // Template for sending out the reply.
  template <typename ReplyProtobufType>
  void SendReply(
      std::unique_ptr<DBusMethodResponse<ReplyProtobufType>> response,
      const ReplyProtobufType& reply,
      bool quit_daemon) {
    response->Return(reply);

    if (quit_daemon) {
      PostQuitTask();
    }
  }

  // Schedule an asynchronous D-Bus shutdown and exit the daemon.
  void PostQuitTask();

  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;

  // D-Bus signals.
  std::weak_ptr<brillo::dbus_utils::DBusSignal<RmadErrorCode>> error_signal_;
  std::weak_ptr<brillo::dbus_utils::DBusSignal<HardwareVerificationResult>>
      hardware_verification_signal_;
  std::weak_ptr<brillo::dbus_utils::DBusSignal<UpdateRoFirmwareStatus>>
      update_ro_firmware_status_signal_;
  std::weak_ptr<brillo::dbus_utils::DBusSignal<CalibrationOverallStatus>>
      calibration_overall_signal_;
  std::weak_ptr<brillo::dbus_utils::DBusSignal<CalibrationComponentStatus>>
      calibration_component_signal_;
  std::weak_ptr<brillo::dbus_utils::DBusSignal<ProvisionStatus>>
      provision_signal_;
  std::weak_ptr<brillo::dbus_utils::DBusSignal<FinalizeStatus>>
      finalize_signal_;
  std::weak_ptr<brillo::dbus_utils::DBusSignal<bool>> hwwp_signal_;
  std::weak_ptr<brillo::dbus_utils::DBusSignal<bool>> power_cable_signal_;

  // RMA interface for handling most of the D-Bus requests.
  RmadInterface* rmad_interface_;
  // RMA state file path.
  base::FilePath state_file_path_;
  // External utils to communicate with tpm_manager.
  std::unique_ptr<TpmManagerClient> tpm_manager_client_;
  // External utils to get cros_config data.
  std::unique_ptr<CrosConfigUtils> cros_config_utils_;
  // External utils to get crossystem data.
  std::unique_ptr<CrosSystemUtils> crossystem_utils_;
  // External utils initialization status.
  bool is_external_utils_initialized_;
  // RMA interface setup status. Only set up the interface when RMA is required
  // to avoid unnecessary code paths.
  bool is_interface_set_up_;
  // Whether the device should trigger shimless RMA.
  bool is_rma_required_;

  // Test mode daemon.
  bool test_mode_;

  base::WeakPtrFactory<DBusService> weak_ptr_factory_{this};
};

}  // namespace rmad

#endif  // RMAD_DBUS_SERVICE_H_
