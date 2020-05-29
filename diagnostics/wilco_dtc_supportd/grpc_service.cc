// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/wilco_dtc_supportd/grpc_service.h"

#include <fcntl.h>
#include <unistd.h>
#include <cstdint>
#include <iterator>
#include <utility>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_util.h>

#include "diagnostics/wilco_dtc_supportd/ec_constants.h"
#include "diagnostics/wilco_dtc_supportd/telemetry/system_files_service_impl.h"
#include "diagnostics/wilco_dtc_supportd/telemetry/system_info_service_impl.h"

#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// The total size of "string" and "bytes" fields in one
// PerformWebRequestParameter must not exceed 1MB.
const int kMaxPerformWebRequestParameterSizeInBytes = 1000 * 1000;

// The maximum number of header in PerformWebRequestParameter.
const int kMaxNumberOfHeadersInPerformWebRequestParameter = 1000 * 1000;

namespace {

using SendMessageToUiCallback = GrpcService::SendMessageToUiCallback;
using PerformWebRequestResponseCallback =
    GrpcService::PerformWebRequestResponseCallback;
using DelegateWebRequestStatus = GrpcService::Delegate::WebRequestStatus;
using DelegateWebRequestHttpMethod =
    GrpcService::Delegate::WebRequestHttpMethod;
using GetAvailableRoutinesCallback = GrpcService::GetAvailableRoutinesCallback;
using RunRoutineCallback = GrpcService::RunRoutineCallback;
using GetRoutineUpdateCallback = GrpcService::GetRoutineUpdateCallback;
using GetConfigurationDataCallback = GrpcService::GetConfigurationDataCallback;
using GetDriveSystemDataCallback = GrpcService::GetDriveSystemDataCallback;
using RequestBluetoothDataNotificationCallback =
    GrpcService::RequestBluetoothDataNotificationCallback;
using GetStatefulPartitionAvailableCapacityCallback =
    GrpcService::GetStatefulPartitionAvailableCapacityCallback;

// Https prefix expected to be a prefix of URL in PerformWebRequestParameter.
constexpr char kHttpsPrefix[] = "https://";

// Calculates the size of all "string" and "bytes" fields in the request.
// Must be updated if grpc_api::PerformWebRequestParameter proto is updated.
int64_t CalculateWebRequestParameterSize(
    const std::unique_ptr<grpc_api::PerformWebRequestParameter>& parameter) {
  int64_t size = parameter->url().length() + parameter->request_body().size();
  for (const std::string& header : parameter->headers()) {
    size += header.length();
  }
  return size;
}

// Forwards and wraps the result of a SendMessageToUi into gRPC response.
void ForwardSendMessageToUiResponse(const SendMessageToUiCallback& callback,
                                    base::StringPiece response_json_message) {
  auto reply = std::make_unique<grpc_api::SendMessageToUiResponse>();
  reply->set_response_json_message(response_json_message.as_string());
  callback.Run(std::move(reply));
}

// Forwards and wraps status & HTTP status into gRPC PerformWebRequestResponse.
void ForwardWebGrpcResponse(const PerformWebRequestResponseCallback& callback,
                            DelegateWebRequestStatus status,
                            int http_status,
                            base::StringPiece response_body) {
  auto reply = std::make_unique<grpc_api::PerformWebRequestResponse>();
  switch (status) {
    case DelegateWebRequestStatus::kOk:
      reply->set_status(grpc_api::PerformWebRequestResponse::STATUS_OK);
      reply->set_http_status(http_status);
      reply->set_response_body(response_body.as_string());
      break;
    case DelegateWebRequestStatus::kNetworkError:
      reply->set_status(
          grpc_api::PerformWebRequestResponse::STATUS_NETWORK_ERROR);
      break;
    case DelegateWebRequestStatus::kHttpError:
      reply->set_status(grpc_api::PerformWebRequestResponse::STATUS_HTTP_ERROR);
      reply->set_http_status(http_status);
      reply->set_response_body(response_body.as_string());
      break;
    case DelegateWebRequestStatus::kInternalError:
      reply->set_status(
          grpc_api::PerformWebRequestResponse::STATUS_INTERNAL_ERROR);
      break;
  }
  callback.Run(std::move(reply));
}

// Converts gRPC HTTP method into GrpcService::Delegate's HTTP
// method, returns false if HTTP method is invalid.
bool GetDelegateWebRequestHttpMethod(
    grpc_api::PerformWebRequestParameter::HttpMethod http_method,
    DelegateWebRequestHttpMethod* delegate_http_method) {
  switch (http_method) {
    case grpc_api::PerformWebRequestParameter::HTTP_METHOD_GET:
      *delegate_http_method = DelegateWebRequestHttpMethod::kGet;
      return true;
    case grpc_api::PerformWebRequestParameter::HTTP_METHOD_HEAD:
      *delegate_http_method = DelegateWebRequestHttpMethod::kHead;
      return true;
    case grpc_api::PerformWebRequestParameter::HTTP_METHOD_POST:
      *delegate_http_method = DelegateWebRequestHttpMethod::kPost;
      return true;
    case grpc_api::PerformWebRequestParameter::HTTP_METHOD_PUT:
      *delegate_http_method = DelegateWebRequestHttpMethod::kPut;
      return true;
    default:
      LOG(ERROR) << "The HTTP method is unset or invalid: "
                 << static_cast<int>(http_method);
      return false;
  }
}

// Converts gRPC VPD field into SystemFilesService's VpdField, returns false if
// VPD field is invalid.
bool GetSystemFilesServiceVpdField(
    grpc_api::GetVpdFieldRequest::VpdField vpd_field,
    SystemFilesService::VpdField* out_vpd_field) {
  switch (vpd_field) {
    case grpc_api::GetVpdFieldRequest::FIELD_SERIAL_NUMBER:
      *out_vpd_field = SystemFilesService::VpdField::kSerialNumber;
      return true;
    case grpc_api::GetVpdFieldRequest::FIELD_MODEL_NAME:
      *out_vpd_field = SystemFilesService::VpdField::kModelName;
      return true;
    case grpc_api::GetVpdFieldRequest::FIELD_ASSET_ID:
      *out_vpd_field = SystemFilesService::VpdField::kAssetId;
      return true;
    case grpc_api::GetVpdFieldRequest::FIELD_SKU_NUMBER:
      *out_vpd_field = SystemFilesService::VpdField::kSkuNumber;
      return true;
    case grpc_api::GetVpdFieldRequest::FIELD_UUID_ID:
      *out_vpd_field = SystemFilesService::VpdField::kUuid;
      return true;
    case grpc_api::GetVpdFieldRequest::FIELD_MANUFACTURE_DATE:
      *out_vpd_field = SystemFilesService::VpdField::kMfgDate;
      return true;
    case grpc_api::GetVpdFieldRequest::FIELD_ACTIVATE_DATE:
      *out_vpd_field = SystemFilesService::VpdField::kActivateDate;
      return true;
    case grpc_api::GetVpdFieldRequest::FIELD_SYSTEM_ID:
      *out_vpd_field = SystemFilesService::VpdField::kSystemId;
      return true;
    case grpc_api::GetVpdFieldRequest::FIELD_UNSET:
    default:
      return false;
  }
}

// Forwards and wraps available routines into a gRPC response.
void ForwardGetAvailableRoutinesResponse(
    const GetAvailableRoutinesCallback& callback,
    const std::vector<grpc_api::DiagnosticRoutine>& routines,
    grpc_api::RoutineServiceStatus service_status) {
  auto reply = std::make_unique<grpc_api::GetAvailableRoutinesResponse>();
  for (auto routine : routines)
    reply->add_routines(routine);
  reply->set_service_status(service_status);
  callback.Run(std::move(reply));
}

// Forwards and wraps the result of a RunRoutine command into a gRPC response.
void ForwardRunRoutineResponse(const RunRoutineCallback& callback,
                               int uuid,
                               grpc_api::DiagnosticRoutineStatus status,
                               grpc_api::RoutineServiceStatus service_status) {
  auto reply = std::make_unique<grpc_api::RunRoutineResponse>();
  reply->set_uuid(uuid);
  reply->set_status(status);
  reply->set_service_status(service_status);
  callback.Run(std::move(reply));
}

// Forwards and wraps the results of a GetRoutineUpdate command into a gRPC
// response.
void ForwardGetRoutineUpdateResponse(
    const GetRoutineUpdateCallback& callback,
    int uuid,
    grpc_api::DiagnosticRoutineStatus status,
    int progress_percent,
    grpc_api::DiagnosticRoutineUserMessage user_message,
    const std::string& output,
    const std::string& status_message,
    grpc_api::RoutineServiceStatus service_status) {
  auto reply = std::make_unique<grpc_api::GetRoutineUpdateResponse>();
  reply->set_uuid(uuid);
  reply->set_status(status);
  reply->set_progress_percent(progress_percent);
  reply->set_user_message(user_message);
  reply->set_output(output);
  reply->set_status_message(status_message);
  reply->set_service_status(service_status);
  callback.Run(std::move(reply));
}

// Forwards and wraps the result of a GetConfigurationDataFromBrowser into gRPC
// response.
void ForwardGetConfigurationDataResponse(
    const GetConfigurationDataCallback& callback,
    const std::string& json_configuration_data) {
  auto reply = std::make_unique<grpc_api::GetConfigurationDataResponse>();
  reply->set_json_configuration_data(json_configuration_data);
  callback.Run(std::move(reply));
}

// Forwards and wraps the result of a GetDriveSystemData into gRPC
// response.
void ForwardGetDriveSystemDataResponse(
    const GetDriveSystemDataCallback& callback,
    const std::string& payload,
    bool success) {
  auto reply = std::make_unique<grpc_api::GetDriveSystemDataResponse>();
  if (success) {
    reply->set_status(grpc_api::GetDriveSystemDataResponse::STATUS_OK);
    reply->set_payload(payload);
  } else {
    reply->set_status(
        grpc_api::GetDriveSystemDataResponse::STATUS_ERROR_REQUEST_PROCESSING);
  }
  callback.Run(std::move(reply));
}

// Extracts stateful partition info from cros_healthd's TelemetryInfo
// and moves it into a gRPC response.
void ForwardGetStatefulPartitionAvailableCapacity(
    const GetStatefulPartitionAvailableCapacityCallback& callback,
    chromeos::cros_healthd::mojom::TelemetryInfoPtr info) {
  auto reply = std::make_unique<
      grpc_api::GetStatefulPartitionAvailableCapacityResponse>();

  if (!info || !info->stateful_partition_result ||
      !info->stateful_partition_result->is_partition_info()) {
    reply->set_status(grpc_api::GetStatefulPartitionAvailableCapacityResponse::
                          STATUS_ERROR_REQUEST_PROCESSING);
    callback.Run(std::move(reply));
    return;
  }

  reply->set_status(
      grpc_api::GetStatefulPartitionAvailableCapacityResponse::STATUS_OK);
  // Reduce to MiB and round down to multiple of 100MiB.
  uint64_t available_space =
      info->stateful_partition_result->get_partition_info()->available_space;
  reply->set_available_capacity_mb((available_space / 1024 / 1024 / 100) * 100);

  callback.Run(std::move(reply));
}

}  // namespace

GrpcService::GrpcService(Delegate* delegate)
    : delegate_(delegate),
      system_files_service_(new SystemFilesServiceImpl()),
      system_info_service_(new SystemInfoServiceImpl()) {
  DCHECK(delegate_);
}

GrpcService::~GrpcService() = default;

// Overrides the file system root directory for file operations in tests.
void GrpcService::set_root_dir_for_testing(const base::FilePath& root_dir) {
  root_dir_ = root_dir;

  auto system_files_service = std::make_unique<SystemFilesServiceImpl>();
  system_files_service->set_root_dir_for_testing(root_dir);

  set_system_files_service_for_testing(std::move(system_files_service));
}

// Overrides the system files service for operations in tests.
void GrpcService::set_system_files_service_for_testing(
    std::unique_ptr<SystemFilesService> service) {
  system_files_service_ = std::move(service);
}

void GrpcService::set_system_info_service_for_testing(
    std::unique_ptr<SystemInfoService> service) {
  system_info_service_ = std::move(service);
}

void GrpcService::SendMessageToUi(
    std::unique_ptr<grpc_api::SendMessageToUiRequest> request,
    const SendMessageToUiCallback& callback) {
  delegate_->SendWilcoDtcMessageToUi(
      request->json_message(),
      base::Bind(&ForwardSendMessageToUiResponse, callback));
}

void GrpcService::GetProcData(
    std::unique_ptr<grpc_api::GetProcDataRequest> request,
    const GetProcDataCallback& callback) {
  DCHECK(request);
  auto reply = std::make_unique<grpc_api::GetProcDataResponse>();
  switch (request->type()) {
    case grpc_api::GetProcDataRequest::FILE_UPTIME:
      AddFileDump(SystemFilesService::File::kProcUptime,
                  reply->mutable_file_dump());
      break;
    case grpc_api::GetProcDataRequest::FILE_MEMINFO:
      AddFileDump(SystemFilesService::File::kProcMeminfo,
                  reply->mutable_file_dump());
      break;
    case grpc_api::GetProcDataRequest::FILE_LOADAVG:
      AddFileDump(SystemFilesService::File::kProcLoadavg,
                  reply->mutable_file_dump());
      break;
    case grpc_api::GetProcDataRequest::FILE_STAT:
      AddFileDump(SystemFilesService::File::kProcStat,
                  reply->mutable_file_dump());
      break;
    case grpc_api::GetProcDataRequest::DIRECTORY_ACPI_BUTTON:
      AddDirectoryDump(SystemFilesService::Directory::kProcAcpiButton,
                       reply->mutable_file_dump());
      break;
    case grpc_api::GetProcDataRequest::FILE_NET_NETSTAT:
      AddFileDump(SystemFilesService::File::kProcNetNetstat,
                  reply->mutable_file_dump());
      break;
    case grpc_api::GetProcDataRequest::FILE_NET_DEV:
      AddFileDump(SystemFilesService::File::kProcNetDev,
                  reply->mutable_file_dump());
      break;
    case grpc_api::GetProcDataRequest::FILE_DISKSTATS:
      AddFileDump(SystemFilesService::File::kProcDiskstats,
                  reply->mutable_file_dump());
      break;
    case grpc_api::GetProcDataRequest::FILE_CPUINFO:
      AddFileDump(SystemFilesService::File::kProcCpuinfo,
                  reply->mutable_file_dump());
      break;
    case grpc_api::GetProcDataRequest::FILE_VMSTAT:
      AddFileDump(SystemFilesService::File::kProcVmstat,
                  reply->mutable_file_dump());
      break;
    default:
      LOG(ERROR) << "GetProcData gRPC request type unset or invalid: "
                 << request->type();
      // Error is designated by a reply with the empty list of entries.
      callback.Run(std::move(reply));
      return;
  }
  VLOG(1) << "Completing GetProcData gRPC request of type " << request->type()
          << ", returning " << reply->file_dump_size() << " items";
  callback.Run(std::move(reply));
}

void GrpcService::GetSysfsData(
    std::unique_ptr<grpc_api::GetSysfsDataRequest> request,
    const GetSysfsDataCallback& callback) {
  DCHECK(request);
  auto reply = std::make_unique<grpc_api::GetSysfsDataResponse>();
  switch (request->type()) {
    case grpc_api::GetSysfsDataRequest::CLASS_HWMON:
      AddDirectoryDump(SystemFilesService::Directory::kSysClassHwmon,
                       reply->mutable_file_dump());
      break;
    case grpc_api::GetSysfsDataRequest::CLASS_THERMAL:
      AddDirectoryDump(SystemFilesService::Directory::kSysClassThermal,
                       reply->mutable_file_dump());
      break;
    case grpc_api::GetSysfsDataRequest::FIRMWARE_DMI_TABLES:
      AddDirectoryDump(SystemFilesService::Directory::kSysFirmwareDmiTables,
                       reply->mutable_file_dump());
      break;
    case grpc_api::GetSysfsDataRequest::CLASS_POWER_SUPPLY:
      AddDirectoryDump(SystemFilesService::Directory::kSysClassPowerSupply,
                       reply->mutable_file_dump());
      break;
    case grpc_api::GetSysfsDataRequest::CLASS_BACKLIGHT:
      AddDirectoryDump(SystemFilesService::Directory::kSysClassBacklight,
                       reply->mutable_file_dump());
      break;
    case grpc_api::GetSysfsDataRequest::CLASS_NETWORK:
      AddDirectoryDump(SystemFilesService::Directory::kSysClassNetwork,
                       reply->mutable_file_dump());
      break;
    case grpc_api::GetSysfsDataRequest::DEVICES_SYSTEM_CPU:
      AddDirectoryDump(SystemFilesService::Directory::kSysDevicesSystemCpu,
                       reply->mutable_file_dump());
      break;
    default:
      LOG(ERROR) << "GetSysfsData gRPC request type unset or invalid: "
                 << request->type();
      // Error is designated by a reply with the empty list of entries.
      callback.Run(std::move(reply));
      return;
  }
  VLOG(1) << "Completing GetSysfsData gRPC request of type " << request->type()
          << ", returning " << reply->file_dump_size() << " items";
  callback.Run(std::move(reply));
}

void GrpcService::GetEcTelemetry(
    std::unique_ptr<grpc_api::GetEcTelemetryRequest> request,
    const GetEcTelemetryCallback& callback) {
  DCHECK(request);
  auto reply = std::make_unique<grpc_api::GetEcTelemetryResponse>();
  if (request->payload().empty()) {
    LOG(ERROR) << "GetEcTelemetry gRPC request payload is empty";
    reply->set_status(
        grpc_api::GetEcTelemetryResponse::STATUS_ERROR_INPUT_PAYLOAD_EMPTY);
    callback.Run(std::move(reply));
    return;
  }
  if (request->payload().length() > kEcGetTelemetryPayloadMaxSize) {
    LOG(ERROR) << "GetEcTelemetry gRPC request payload size is exceeded: "
               << request->payload().length() << " vs "
               << kEcGetTelemetryPayloadMaxSize << " allowed";
    reply->set_status(grpc_api::GetEcTelemetryResponse::
                          STATUS_ERROR_INPUT_PAYLOAD_MAX_SIZE_EXCEEDED);
    callback.Run(std::move(reply));
    return;
  }

  base::FilePath telemetry_file_path =
      root_dir_.Append(kEcGetTelemetryFilePath);

  // Use base::ScopedFD to operate with non-seekable files.
  base::ScopedFD telemetry_file(
      HANDLE_EINTR(open(telemetry_file_path.value().c_str(), O_RDWR)));

  if (!telemetry_file.is_valid()) {
    VPLOG(2) << "GetEcTelemetry gRPC can not open the "
             << "telemetry node: " << telemetry_file_path.value();
    reply->set_status(
        grpc_api::GetEcTelemetryResponse::STATUS_ERROR_ACCESSING_DRIVER);
    callback.Run(std::move(reply));
    return;
  }

  int write_result =
      HANDLE_EINTR(write(telemetry_file.get(), request->payload().c_str(),
                         request->payload().length()));
  if (write_result != request->payload().length()) {
    VPLOG(2) << "GetEcTelemetry gRPC can not write request payload to the "
             << "telemetry node: " << telemetry_file_path.value();
    reply->set_status(
        grpc_api::GetEcTelemetryResponse::STATUS_ERROR_ACCESSING_DRIVER);
    callback.Run(std::move(reply));
    return;
  }

  // Reply payload must be empty in case of any failure.
  char file_content[kEcGetTelemetryPayloadMaxSize];
  int read_result = HANDLE_EINTR(
      read(telemetry_file.get(), file_content, kEcGetTelemetryPayloadMaxSize));
  if (read_result > 0) {
    reply->set_status(grpc_api::GetEcTelemetryResponse::STATUS_OK);
    reply->set_payload(file_content, read_result);
  } else {
    VPLOG(2) << "GetEcTelemetry gRPC can not read EC telemetry command "
             << "response from telemetry node: " << telemetry_file_path.value();
    reply->set_status(
        grpc_api::GetEcTelemetryResponse::STATUS_ERROR_ACCESSING_DRIVER);
  }
  callback.Run(std::move(reply));
}

void GrpcService::PerformWebRequest(
    std::unique_ptr<grpc_api::PerformWebRequestParameter> parameter,
    const PerformWebRequestResponseCallback& callback) {
  DCHECK(parameter);
  auto reply = std::make_unique<grpc_api::PerformWebRequestResponse>();

  if (parameter->url().empty()) {
    LOG(ERROR) << "PerformWebRequest URL is empty.";
    reply->set_status(
        grpc_api::PerformWebRequestResponse::STATUS_ERROR_INVALID_URL);
    callback.Run(std::move(reply));
    return;
  }
  if (!base::StartsWith(parameter->url(), kHttpsPrefix,
                        base::CompareCase::INSENSITIVE_ASCII)) {
    LOG(ERROR) << "PerformWebRequest URL must be an HTTPS URL.";
    reply->set_status(
        grpc_api::PerformWebRequestResponse::STATUS_ERROR_INVALID_URL);
    callback.Run(std::move(reply));
    return;
  }
  if (parameter->headers().size() >
      kMaxNumberOfHeadersInPerformWebRequestParameter) {
    LOG(ERROR) << "PerformWebRequest number of headers is too large.";
    reply->set_status(
        grpc_api::PerformWebRequestResponse::STATUS_ERROR_MAX_SIZE_EXCEEDED);
    callback.Run(std::move(reply));
    return;
  }
  if (CalculateWebRequestParameterSize(parameter) >
      kMaxPerformWebRequestParameterSizeInBytes) {
    LOG(ERROR) << "PerformWebRequest request is too large.";
    reply->set_status(
        grpc_api::PerformWebRequestResponse::STATUS_ERROR_MAX_SIZE_EXCEEDED);
    callback.Run(std::move(reply));
    return;
  }

  DelegateWebRequestHttpMethod delegate_http_method;
  if (!GetDelegateWebRequestHttpMethod(parameter->http_method(),
                                       &delegate_http_method)) {
    reply->set_status(grpc_api::PerformWebRequestResponse ::
                          STATUS_ERROR_REQUIRED_FIELD_MISSING);
    callback.Run(std::move(reply));
    return;
  }
  delegate_->PerformWebRequestToBrowser(
      delegate_http_method, parameter->url(),
      std::vector<std::string>(
          std::make_move_iterator(parameter->mutable_headers()->begin()),
          std::make_move_iterator(parameter->mutable_headers()->end())),
      parameter->request_body(), base::Bind(&ForwardWebGrpcResponse, callback));
}

void GrpcService::GetAvailableRoutines(
    std::unique_ptr<grpc_api::GetAvailableRoutinesRequest> request,
    const GetAvailableRoutinesCallback& callback) {
  DCHECK(request);
  delegate_->GetAvailableRoutinesToService(
      base::Bind(&ForwardGetAvailableRoutinesResponse, callback));
}

void GrpcService::RunRoutine(
    std::unique_ptr<grpc_api::RunRoutineRequest> request,
    const RunRoutineCallback& callback) {
  DCHECK(request);

  // Make sure the RunRoutineRequest is superficially valid.
  switch (request->routine()) {
    case grpc_api::ROUTINE_BATTERY:
      if (!request->has_battery_params()) {
        LOG(ERROR) << "RunRoutineRequest with routine type BATTERY has no "
                      "battery parameters.";
        ForwardRunRoutineResponse(callback, 0 /* uuid */,
                                  grpc_api::ROUTINE_STATUS_INVALID_FIELD,
                                  grpc_api::ROUTINE_SERVICE_STATUS_OK);
        return;
      }
      break;
    case grpc_api::ROUTINE_BATTERY_SYSFS:
      if (!request->has_battery_sysfs_params()) {
        LOG(ERROR) << "RunRoutineRequest with routine type BATTERY_SYSFS has "
                      "no battery_sysfs parameters.";
        ForwardRunRoutineResponse(callback, 0 /* uuid */,
                                  grpc_api::ROUTINE_STATUS_INVALID_FIELD,
                                  grpc_api::ROUTINE_SERVICE_STATUS_OK);
        return;
      }
      break;
    case grpc_api::ROUTINE_URANDOM:
      if (!request->has_urandom_params()) {
        LOG(ERROR) << "RunRoutineRequest with routine type URANDOM has no "
                      "urandom parameters.";
        ForwardRunRoutineResponse(callback, 0 /* uuid */,
                                  grpc_api::ROUTINE_STATUS_INVALID_FIELD,
                                  grpc_api::ROUTINE_SERVICE_STATUS_OK);
        return;
      }
      break;
    case grpc_api::ROUTINE_SMARTCTL_CHECK:
      if (!request->has_smartctl_check_params()) {
        LOG(ERROR) << "RunRoutineRequest with routine type SMARTCTL_CHECK "
                      "has no smartctl_check parameters.";
        ForwardRunRoutineResponse(callback, 0 /* uuid */,
                                  grpc_api::ROUTINE_STATUS_INVALID_FIELD,
                                  grpc_api::ROUTINE_SERVICE_STATUS_OK);
        return;
      }
      break;
    case grpc_api::ROUTINE_CPU_CACHE:
      if (!request->has_cpu_params()) {
        LOG(ERROR) << "RunRoutineRequest with routine type CPU CACHE "
                      "has no cpu parameters.";
        ForwardRunRoutineResponse(callback, 0 /* uuid */,
                                  grpc_api::ROUTINE_STATUS_INVALID_FIELD,
                                  grpc_api::ROUTINE_SERVICE_STATUS_OK);
        return;
      }
      break;
    case grpc_api::ROUTINE_CPU_STRESS:
      if (!request->has_cpu_params()) {
        LOG(ERROR) << "RunRoutineRequest with routine type CPU STRESS "
                      "has no cpu parameters.";
        ForwardRunRoutineResponse(callback, 0 /* uuid */,
                                  grpc_api::ROUTINE_STATUS_INVALID_FIELD,
                                  grpc_api::ROUTINE_SERVICE_STATUS_OK);
        return;
      }
      break;
    case grpc_api::ROUTINE_FLOATING_POINT_ACCURACY:
      if (!request->has_floating_point_accuracy_params()) {
        LOG(ERROR) << "RunRoutineRequest with routine type "
                      "FLOATING_POINT_ACCURACY has no "
                      "floating_point_accuracy parameters.";
        ForwardRunRoutineResponse(callback, 0 /* uuid */,
                                  grpc_api::ROUTINE_STATUS_INVALID_FIELD,
                                  grpc_api::ROUTINE_SERVICE_STATUS_OK);
        return;
      }
      break;
    case grpc_api::ROUTINE_NVME_WEAR_LEVEL:
      if (!request->has_nvme_wear_level_params()) {
        LOG(ERROR) << "RunRoutineRequest with routine type "
                      "ROUTINE_NVME_WEAR_LEVEL has no nvme_wear_level "
                      "parameters.";
        ForwardRunRoutineResponse(callback, 0 /* uuid */,
                                  grpc_api::ROUTINE_STATUS_INVALID_FIELD,
                                  grpc_api::ROUTINE_SERVICE_STATUS_OK);
        return;
      }
      break;
    case grpc_api::ROUTINE_NVME_SHORT_SELF_TEST:
      if (!request->has_nvme_short_self_test_params()) {
        LOG(ERROR) << "RunRoutineRequest with routine type "
                      "ROUTINE_NVME_SHORT_SELF_TEST has no "
                      "nvme_short_self_test parameters.";
        ForwardRunRoutineResponse(callback, 0 /* uuid */,
                                  grpc_api::ROUTINE_STATUS_INVALID_FIELD,
                                  grpc_api::ROUTINE_SERVICE_STATUS_OK);
        return;
      }
      break;
    case grpc_api::ROUTINE_NVME_LONG_SELF_TEST:
      if (!request->has_nvme_long_self_test_params()) {
        LOG(ERROR) << "RunRoutineRequest with routine type "
                      "ROUTINE_NVME_LONG_SELF_TEST has no "
                      "nvme_long_self_test parameters.";
        ForwardRunRoutineResponse(callback, 0 /* uuid */,
                                  grpc_api::ROUTINE_STATUS_INVALID_FIELD,
                                  grpc_api::ROUTINE_SERVICE_STATUS_OK);
        return;
      }
      break;
    case grpc_api::ROUTINE_DISK_LINEAR_READ:
      if (!request->has_disk_linear_read_params()) {
        LOG(ERROR) << "RunRoutineRequest with routine type LINEAR_READ "
                      "has no linear_read parameters.";
        ForwardRunRoutineResponse(callback, 0 /* uuid */,
                                  grpc_api::ROUTINE_STATUS_INVALID_FIELD,
                                  grpc_api::ROUTINE_SERVICE_STATUS_OK);
        return;
      }
      break;
    case grpc_api::ROUTINE_DISK_RANDOM_READ:
      if (!request->has_disk_random_read_params()) {
        LOG(ERROR) << "RunRoutineRequest with routine type RANDOM_READ "
                      "has no random_read parameters.";
        ForwardRunRoutineResponse(callback, 0 /* uuid */,
                                  grpc_api::ROUTINE_STATUS_INVALID_FIELD,
                                  grpc_api::ROUTINE_SERVICE_STATUS_OK);
        return;
      }
      break;
    case grpc_api::ROUTINE_PRIME_SEARCH:
      if (!request->has_prime_search_params()) {
        LOG(ERROR) << "RunRoutineRequest with routine type PRIME_SEARCH "
                      "has no prime_search parameters.";
        ForwardRunRoutineResponse(callback, 0 /* uuid */,
                                  grpc_api::ROUTINE_STATUS_INVALID_FIELD,
                                  grpc_api::ROUTINE_SERVICE_STATUS_OK);
        return;
      }
      break;
    default:
      LOG(ERROR) << "RunRoutineRequest routine type invalid or unset.";
      ForwardRunRoutineResponse(callback, 0 /* uuid */,
                                grpc_api::ROUTINE_STATUS_INVALID_FIELD,
                                grpc_api::ROUTINE_SERVICE_STATUS_OK);
      return;
  }

  delegate_->RunRoutineToService(
      *request, base::Bind(&ForwardRunRoutineResponse, callback));
}

void GrpcService::GetRoutineUpdate(
    std::unique_ptr<grpc_api::GetRoutineUpdateRequest> request,
    const GetRoutineUpdateCallback& callback) {
  DCHECK(request);

  if (request->command() == grpc_api::GetRoutineUpdateRequest::COMMAND_UNSET) {
    ForwardGetRoutineUpdateResponse(
        callback, request->uuid(), grpc_api::ROUTINE_STATUS_INVALID_FIELD,
        0 /* progress_percent */, grpc_api::ROUTINE_USER_MESSAGE_UNSET,
        "" /* output */, "No command specified.",
        grpc_api::ROUTINE_SERVICE_STATUS_OK);
    return;
  }

  delegate_->GetRoutineUpdateRequestToService(
      request->uuid(), request->command(), request->include_output(),
      base::Bind(&ForwardGetRoutineUpdateResponse, callback));
}

void GrpcService::GetOsVersion(
    std::unique_ptr<grpc_api::GetOsVersionRequest> request,
    const GetOsVersionCallback& callback) {
  DCHECK(request);

  auto reply = std::make_unique<grpc_api::GetOsVersionResponse>();

  std::string version;
  if (system_info_service_->GetOsVersion(&version)) {
    reply->set_version(version);
  }

  int milestone = 0;
  if (system_info_service_->GetOsMilestone(&milestone)) {
    reply->set_milestone(milestone);
  }

  callback.Run(std::move(reply));
}

void GrpcService::GetConfigurationData(
    std::unique_ptr<grpc_api::GetConfigurationDataRequest> request,
    const GetConfigurationDataCallback& callback) {
  DCHECK(request);

  delegate_->GetConfigurationDataFromBrowser(
      base::Bind(&ForwardGetConfigurationDataResponse, callback));
}

void GrpcService::GetVpdField(
    std::unique_ptr<grpc_api::GetVpdFieldRequest> request,
    const GetVpdFieldCallback& callback) {
  DCHECK(request);

  auto reply = std::make_unique<grpc_api::GetVpdFieldResponse>();

  SystemFilesService::VpdField vpd_field;
  if (!GetSystemFilesServiceVpdField(request->vpd_field(), &vpd_field)) {
    VLOG(1) << "The VPD field is unspecified or invalid: "
            << static_cast<int>(request->vpd_field());
    reply->set_status(
        grpc_api::GetVpdFieldResponse::STATUS_ERROR_VPD_FIELD_UNKNOWN);
    callback.Run(std::move(reply));
    return;
  }

  auto result = system_files_service_->GetVpdField(vpd_field);
  if (!result.has_value()) {
    VPLOG(2) << "Failed to read VPD field "
             << static_cast<int>(request->vpd_field());
    reply->set_status(grpc_api::GetVpdFieldResponse::STATUS_ERROR_INTERNAL);
    callback.Run(std::move(reply));
    return;
  }

  reply->set_status(grpc_api::GetVpdFieldResponse::STATUS_OK);
  reply->set_vpd_field_value(std::move(result.value()));

  callback.Run(std::move(reply));
}

void GrpcService::GetDriveSystemData(
    std::unique_ptr<grpc_api::GetDriveSystemDataRequest> request,
    const GetDriveSystemDataCallback& callback) {
  DCHECK(request);

  Delegate::DriveSystemDataType data_type;
  switch (request->type()) {
    case grpc_api::GetDriveSystemDataRequest::SMART_ATTRIBUTES:
      data_type = Delegate::DriveSystemDataType::kSmartAttributes;
      break;
    case grpc_api::GetDriveSystemDataRequest::IDENTITY_ATTRIBUTES:
      data_type = Delegate::DriveSystemDataType::kIdentityAttributes;
      break;
    default:
      LOG(ERROR) << "The GetDriveSystemDataRequest::Type is unset or invalid: "
                 << static_cast<int>(request->type());
      auto reply = std::make_unique<grpc_api::GetDriveSystemDataResponse>();
      reply->set_status(grpc_api::GetDriveSystemDataResponse::
                            STATUS_ERROR_REQUEST_TYPE_UNKNOWN);
      callback.Run(std::move(reply));
      return;
  }

  delegate_->GetDriveSystemData(
      data_type, base::Bind(&ForwardGetDriveSystemDataResponse, callback));
}

void GrpcService::RequestBluetoothDataNotification(
    std::unique_ptr<grpc_api::RequestBluetoothDataNotificationRequest> request,
    const RequestBluetoothDataNotificationCallback& callback) {
  delegate_->RequestBluetoothDataNotification();

  callback.Run(
      std::make_unique<grpc_api::RequestBluetoothDataNotificationResponse>());
}

void GrpcService::GetStatefulPartitionAvailableCapacity(
    std::unique_ptr<grpc_api::GetStatefulPartitionAvailableCapacityRequest>
        request,
    const GetStatefulPartitionAvailableCapacityCallback& callback) {
  DCHECK(request);

  std::vector<chromeos::cros_healthd::mojom::ProbeCategoryEnum> categories{
      chromeos::cros_healthd::mojom::ProbeCategoryEnum::kStatefulPartition};
  delegate_->ProbeTelemetryInfo(
      std::move(categories),
      base::Bind(&ForwardGetStatefulPartitionAvailableCapacity, callback));
}

void GrpcService::AddFileDump(
    SystemFilesService::File location,
    google::protobuf::RepeatedPtrField<grpc_api::FileDump>* file_dumps) {
  auto file_dump = system_files_service_->GetFileDump(location);
  if (!file_dump)
    return;

  grpc_api::FileDump grpc_dump;
  grpc_dump.set_path(file_dump.value().path.value());
  grpc_dump.set_canonical_path(file_dump.value().canonical_path.value());
  grpc_dump.set_contents(std::move(file_dump.value().contents));

  file_dumps->Add()->Swap(&grpc_dump);
}

void GrpcService::AddDirectoryDump(
    SystemFilesService::Directory location,
    google::protobuf::RepeatedPtrField<grpc_api::FileDump>* grpc_dumps) {
  auto dumps = system_files_service_->GetDirectoryDump(location);
  if (!dumps)
    return;

  for (auto& dump : dumps.value()) {
    grpc_api::FileDump grpc_dump;
    grpc_dump.set_path(dump->path.value());
    grpc_dump.set_canonical_path(dump->canonical_path.value());
    grpc_dump.set_contents(std::move(dump->contents));

    grpc_dumps->Add()->Swap(&grpc_dump);
  }
}

}  // namespace diagnostics
