// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_DBUS_SERVICE_H_
#define RMAD_DBUS_SERVICE_H_

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/dbus/data_serialization.h>
#include <brillo/dbus/dbus_method_response.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/dbus/dbus_signal.h>
#include <dbus/bus.h>

#include "rmad/rmad_interface.h"
#include "rmad/system/tpm_manager_client.h"

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
              std::unique_ptr<TpmManagerClient> tpm_manager_client);
  DBusService(const DBusService&) = delete;
  DBusService& operator=(const DBusService&) = delete;

  ~DBusService() override = default;

  bool SendErrorSignal(RmadErrorCode error);
  bool SendHardwareVerificationResultSignal(
      const HardwareVerificationResult& result);
  bool SendCalibrationSetupSignal(CalibrationSetupInstruction instruction);
  bool SendCalibrationOverallSignal(CalibrationOverallStatus status);
  bool SendCalibrationProgressSignal(CalibrationComponentStatus status);
  bool SendProvisionProgressSignal(const ProvisionStatus& status);
  bool SendFinalizeProgressSignal(const FinalizeStatus& status);
  bool SendHardwareWriteProtectionStateSignal(bool enabled);
  bool SendPowerCableStateSignal(bool plugged_in);

 protected:
  // brillo::DBusServiceDaemon overrides.
  int OnEventLoopStarted() override;
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

  // Provide callbacks for sending signals to rmad_interface.
  void RegisterSignalSenders();

 private:
  friend class DBusServiceTest;

  bool IsRmaRequired() const;
  bool ConditionallySetUpInterface();

  template <typename... Types>
  using DBusMethodResponse = brillo::dbus_utils::DBusMethodResponse<Types...>;

  // Template for handling D-Bus methods.
  template <typename RequestProtobufType, typename ReplyType>
  using HandlerFunction = void (RmadInterface::*)(
      const RequestProtobufType&,
      const base::RepeatingCallback<void(const ReplyType&)>&);

  template <typename RequestProtobufType,
            typename ReplyType,
            DBusService::HandlerFunction<RequestProtobufType, ReplyType> func>
  void HandleMethod(std::unique_ptr<DBusMethodResponse<ReplyType>> response,
                    const RequestProtobufType& request) {
    if (!ConditionallySetUpInterface()) {
      SendErrorSignal(RMAD_ERROR_DAEMON_INITIALIZATION_FAILED);
      return;
    }
    // Convert to shared_ptr so rmad_interface_ can safely copy the callback.
    using SharedResponsePointer =
        std::shared_ptr<DBusMethodResponse<ReplyType>>;
    (rmad_interface_->*func)(
        request, base::BindRepeating(
                     &DBusService::SendReply<ReplyType>, base::Unretained(this),
                     SharedResponsePointer(std::move(response))));
  }

  // Template for handling D-Bus methods without request protobuf.
  template <typename ReplyType>
  using HandlerFunctionEmptyRequest = void (RmadInterface::*)(
      const base::RepeatingCallback<void(const ReplyType&)>&);

  template <typename ReplyType,
            DBusService::HandlerFunctionEmptyRequest<ReplyType> func>
  void HandleMethod(std::unique_ptr<DBusMethodResponse<ReplyType>> response) {
    if (!ConditionallySetUpInterface()) {
      SendErrorSignal(RMAD_ERROR_DAEMON_INITIALIZATION_FAILED);
      return;
    }
    // Convert to shared_ptr so rmad_interface_ can safely copy the callback.
    using SharedResponsePointer =
        std::shared_ptr<DBusMethodResponse<ReplyType>>;
    (rmad_interface_->*func)(base::BindRepeating(
        &DBusService::SendReply<ReplyType>, base::Unretained(this),
        SharedResponsePointer(std::move(response))));
  }

  bool HandleIsRmaRequiredMethod();
  std::string HandleGetLogPathMethod();
  GetLogReply HandleGetLogMethod();

  // Template for sending out the reply.
  template <typename ReplyType>
  void SendReply(std::shared_ptr<DBusMethodResponse<ReplyType>> response,
                 const ReplyType& reply) {
    response->Return(reply);

    // Quit the daemon under some conditions.
    // TODO(chenghan): This is now determined by state. Maybe it's better to
    //                 decide this in the state transition, e.g. pass an
    //                 additional boolean in the |rmad_interface_| callback.
    ConditionallyQuit();
  }

  // Quit the daemon if current state is:
  //   - STATE_NOT_SET: RMA is not required. Quit the daemon to release
  //                    resources.
  //   - kWpDisableComplete: Need to restart the daemon after disabling hardware
  //                         write protection to get more minijail permissions.
  void ConditionallyQuit();

  // Schedule an asynchronous D-Bus shutdown and exit the daemon.
  void PostQuitTask();

  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;

  // D-Bus signals.
  std::weak_ptr<brillo::dbus_utils::DBusSignal<RmadErrorCode>> error_signal_;
  std::weak_ptr<brillo::dbus_utils::DBusSignal<HardwareVerificationResult>>
      hardware_verification_signal_;
  std::weak_ptr<brillo::dbus_utils::DBusSignal<CalibrationSetupInstruction>>
      calibration_setup_signal_;
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
  // External utils initialization status.
  bool is_external_utils_initialized_;
  // RMA interface setup status. Only set up the interface when RMA is required
  // to avoid unnecessary code paths.
  bool is_interface_set_up_;
  // Whether the device should trigger shimless RMA.
  bool is_rma_required_;
};

}  // namespace rmad

#endif  // RMAD_DBUS_SERVICE_H_
