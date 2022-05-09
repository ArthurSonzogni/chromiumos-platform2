// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/wilco_dtc_supportd/fake_wilco_dtc.h"

#include <base/barrier_closure.h>
#include <base/check.h>
#include <base/run_loop.h>
#include <base/threading/thread_task_runner_handle.h>

namespace diagnostics {
namespace wilco {

FakeWilcoDtc::FakeWilcoDtc(const std::string& grpc_server_uri,
                           const std::string& wilco_dtc_supportd_grpc_uri)
    : grpc_server_(base::ThreadTaskRunnerHandle::Get(), {grpc_server_uri}),
      wilco_dtc_supportd_grp_client_(base::ThreadTaskRunnerHandle::Get(),
                                     wilco_dtc_supportd_grpc_uri) {
  grpc_server_.RegisterHandler(
      &grpc_api::WilcoDtc::AsyncService::RequestHandleMessageFromUi,
      base::BindRepeating(&FakeWilcoDtc::HandleMessageFromUi,
                          base::Unretained(this)));
  grpc_server_.RegisterHandler(
      &grpc_api::WilcoDtc::AsyncService::RequestHandleEcNotification,
      base::BindRepeating(&FakeWilcoDtc::HandleEcNotification,
                          base::Unretained(this)));
  grpc_server_.RegisterHandler(
      &grpc_api::WilcoDtc::AsyncService::RequestHandlePowerNotification,
      base::BindRepeating(&FakeWilcoDtc::HandlePowerNotification,
                          base::Unretained(this)));
  grpc_server_.RegisterHandler(
      &grpc_api::WilcoDtc::AsyncService::RequestHandleConfigurationDataChanged,
      base::BindRepeating(&FakeWilcoDtc::HandleConfigurationDataChanged,
                          base::Unretained(this)));
  grpc_server_.RegisterHandler(
      &grpc_api::WilcoDtc::AsyncService::RequestHandleBluetoothDataChanged,
      base::BindRepeating(&FakeWilcoDtc::HandleBluetoothDataChanged,
                          base::Unretained(this)));

  grpc_server_.Start();
}

FakeWilcoDtc::~FakeWilcoDtc() {
  // Wait until both gRPC server and client get shut down.
  base::RunLoop run_loop;
  const base::RepeatingClosure barrier_closure =
      base::BarrierClosure(2, run_loop.QuitClosure());
  grpc_server_.ShutDown(barrier_closure);
  wilco_dtc_supportd_grp_client_.ShutDown(barrier_closure);
  run_loop.Run();
}

void FakeWilcoDtc::SendMessageToUi(
    const grpc_api::SendMessageToUiRequest& request,
    SendMessageToUiCallback callback) {
  wilco_dtc_supportd_grp_client_.CallRpc(
      &grpc_api::WilcoDtcSupportd::Stub::AsyncSendMessageToUi, request,
      callback);
}

void FakeWilcoDtc::GetProcData(const grpc_api::GetProcDataRequest& request,
                               GetProcDataCallback callback) {
  wilco_dtc_supportd_grp_client_.CallRpc(
      &grpc_api::WilcoDtcSupportd::Stub::AsyncGetProcData, request, callback);
}

void FakeWilcoDtc::GetEcTelemetry(
    const grpc_api::GetEcTelemetryRequest& request,
    GetEcTelemetryCallback callback) {
  wilco_dtc_supportd_grp_client_.CallRpc(
      &grpc_api::WilcoDtcSupportd::Stub::AsyncGetEcTelemetry, request,
      callback);
}

void FakeWilcoDtc::PerformWebRequest(
    const grpc_api::PerformWebRequestParameter& parameter,
    const PerformWebRequestResponseCallback& callback) {
  wilco_dtc_supportd_grp_client_.CallRpc(
      &grpc_api::WilcoDtcSupportd::Stub::AsyncPerformWebRequest, parameter,
      callback);
}

void FakeWilcoDtc::GetConfigurationData(
    const grpc_api::GetConfigurationDataRequest& request,
    const GetConfigurationDataCallback& callback) {
  wilco_dtc_supportd_grp_client_.CallRpc(
      &grpc_api::WilcoDtcSupportd::Stub::AsyncGetConfigurationData, request,
      callback);
}

void FakeWilcoDtc::GetDriveSystemData(
    const grpc_api::GetDriveSystemDataRequest& request,
    const GetDriveSystemDataCallback& callback) {
  wilco_dtc_supportd_grp_client_.CallRpc(
      &grpc_api::WilcoDtcSupportd::Stub::AsyncGetDriveSystemData, request,
      callback);
}

void FakeWilcoDtc::RequestBluetoothDataNotification(
    const grpc_api::RequestBluetoothDataNotificationRequest& request,
    const RequestBluetoothDataNotificationCallback& callback) {
  wilco_dtc_supportd_grp_client_.CallRpc(
      &grpc_api::WilcoDtcSupportd::Stub::AsyncRequestBluetoothDataNotification,
      request, callback);
}

void FakeWilcoDtc::GetStatefulPartitionAvailableCapacity(
    const grpc_api::GetStatefulPartitionAvailableCapacityRequest& request,
    const GetStatefulPartitionAvailableCapacityCallback& callback) {
  wilco_dtc_supportd_grp_client_.CallRpc(
      &grpc_api::WilcoDtcSupportd::Stub::
          AsyncGetStatefulPartitionAvailableCapacity,
      request, callback);
}

void FakeWilcoDtc::GetAvailableRoutines(
    const GetAvailableRoutinesCallback& callback) {
  grpc_api::GetAvailableRoutinesRequest request;
  wilco_dtc_supportd_grp_client_.CallRpc(
      &grpc_api::WilcoDtcSupportd::Stub::AsyncGetAvailableRoutines, request,
      callback);
}

void FakeWilcoDtc::HandleMessageFromUi(
    std::unique_ptr<grpc_api::HandleMessageFromUiRequest> request,
    const HandleMessageFromUiCallback& callback) {
  DCHECK(handle_message_from_ui_callback_);
  DCHECK(handle_message_from_ui_json_message_response_.has_value());

  handle_message_from_ui_actual_json_message_.emplace(request->json_message());

  auto response = std::make_unique<grpc_api::HandleMessageFromUiResponse>();
  response->set_response_json_message(
      handle_message_from_ui_json_message_response_.value());
  callback.Run(grpc::Status::OK, std::move(response));

  if (handle_message_from_ui_callback_.has_value())
    std::move(handle_message_from_ui_callback_.value()).Run();
}

void FakeWilcoDtc::HandleEcNotification(
    std::unique_ptr<grpc_api::HandleEcNotificationRequest> request,
    const HandleEcNotificationCallback& callback) {
  DCHECK(handle_ec_event_request_callback_);

  auto response = std::make_unique<grpc_api::HandleEcNotificationResponse>();
  callback.Run(grpc::Status::OK, std::move(response));

  handle_ec_event_request_callback_->Run(request->type(), request->payload());
}

void FakeWilcoDtc::HandlePowerNotification(
    std::unique_ptr<grpc_api::HandlePowerNotificationRequest> request,
    const HandlePowerNotificationCallback& callback) {
  DCHECK(handle_power_event_request_callback_);

  auto response = std::make_unique<grpc_api::HandlePowerNotificationResponse>();
  callback.Run(grpc::Status::OK, std::move(response));

  handle_power_event_request_callback_->Run(request->power_event());
}

void FakeWilcoDtc::HandleConfigurationDataChanged(
    std::unique_ptr<grpc_api::HandleConfigurationDataChangedRequest> request,
    const HandleConfigurationDataChangedCallback& callback) {
  DCHECK(configuration_data_changed_callback_);

  auto response =
      std::make_unique<grpc_api::HandleConfigurationDataChangedResponse>();
  callback.Run(grpc::Status::OK, std::move(response));

  if (configuration_data_changed_callback_.has_value())
    std::move(configuration_data_changed_callback_.value()).Run();
}

void FakeWilcoDtc::HandleBluetoothDataChanged(
    std::unique_ptr<grpc_api::HandleBluetoothDataChangedRequest> request,
    const HandleBluetoothDataChangedCallback& callback) {
  DCHECK(bluetooth_data_changed_request_callback_);

  auto response =
      std::make_unique<grpc_api::HandleBluetoothDataChangedResponse>();
  callback.Run(grpc::Status::OK, std::move(response));

  bluetooth_data_changed_request_callback_->Run(*request);
}

}  // namespace wilco
}  // namespace diagnostics
