// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/missive/missive_impl.h"

#include <cstdlib>
#include <tuple>
#include <utility>

#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/memory/scoped_refptr.h>
#include <base/task/bind_post_task.h>
#include <base/time/time.h>
#include <featured/feature_library.h>

#include "missive/analytics/metrics.h"
#include "missive/analytics/resource_collector_cpu.h"
#include "missive/analytics/resource_collector_memory.h"
#include "missive/analytics/resource_collector_storage.h"
#include "missive/compression/compression_module.h"
#include "missive/dbus/upload_client.h"
#include "missive/encryption/encryption_module.h"
#include "missive/encryption/verification.h"
#include "missive/health/health_module.h"
#include "missive/health/health_module_delegate_impl.h"
#include "missive/missive/migration.h"
#include "missive/missive/missive_args.h"
#include "missive/proto/health.pb.h"
#include "missive/proto/interface.pb.h"
#include "missive/proto/record.pb.h"
#include "missive/scheduler/enqueue_job.h"
#include "missive/scheduler/scheduler.h"
#include "missive/scheduler/upload_job.h"
#include "missive/storage/storage_configuration.h"
#include "missive/storage/storage_module.h"
#include "missive/storage/storage_uploader_interface.h"
#include "missive/util/status.h"
#include "missive/util/statusor.h"

namespace reporting {

namespace {

constexpr CompressionInformation::CompressionAlgorithm kCompressionType =
    CompressionInformation::COMPRESSION_SNAPPY;
constexpr size_t kCompressionThreshold = 512U;

template <class Response>
void HandleResponse(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<Response>>
        out_response,
    Response response_body,
    scoped_refptr<HealthModule> health_module,
    Status status) {
  status.SaveTo(response_body.mutable_status());

  if (!health_module->is_debugging()) {
    out_response->Return(response_body);
    return;
  }

  // Attach health data to response.
  auto response_cb = base::BindPostTaskToCurrentDefault(base::BindOnce(
      [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<Response>>
             out_response,
         Response response_body) { out_response->Return(response_body); },
      std::move(out_response)));
  health_module->GetHealthData(base::BindOnce(
      [](base::OnceCallback<void(Response)> response_cb, Response response_body,
         ERPHealthData health_data) {
        *response_body.mutable_health_data() = std::move(health_data);
        std::move(response_cb).Run(response_body);
      },
      std::move(response_cb), std::move(response_body)));
}

template <typename ResponseType>
ResponseType RespondMissiveDisabled() {
  ResponseType response_body;
  auto* status = response_body.mutable_status();
  status->set_code(error::FAILED_PRECONDITION);
  status->set_error_message("Reporting is disabled");
  return response_body;
}
}  // namespace

MissiveImpl::MissiveImpl() {
  // Constructor may even be called not on any seq task runner.
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

MissiveImpl::~MissiveImpl() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

MissiveImpl& MissiveImpl::SetUploadClientFactory(
    base::OnceCallback<
        void(scoped_refptr<dbus::Bus> bus,
             base::OnceCallback<void(StatusOr<scoped_refptr<UploadClient>>)>
                 callback)> upload_client_factory) {
  upload_client_factory_ = std::move(upload_client_factory);
  return *this;
}
MissiveImpl& MissiveImpl::SetCompressionModuleFactory(
    base::OnceCallback<scoped_refptr<CompressionModule>(
        const MissiveArgs::StorageParameters& parameters)>
        compression_module_factory) {
  compression_module_factory_ = std::move(compression_module_factory);
  return *this;
}
MissiveImpl& MissiveImpl::SetEncryptionModuleFactory(
    base::OnceCallback<scoped_refptr<EncryptionModuleInterface>(
        const MissiveArgs::StorageParameters& parameters)>
        encryption_module_factory) {
  encryption_module_factory_ = std::move(encryption_module_factory);
  return *this;
}
MissiveImpl& MissiveImpl::SetHealthModuleFactory(
    base::OnceCallback<scoped_refptr<HealthModule>(
        const base::FilePath& file_path)> health_module_factory) {
  health_module_factory_ = std::move(health_module_factory);
  return *this;
}
MissiveImpl& MissiveImpl::SetStorageModuleFactory(
    base::OnceCallback<
        void(MissiveImpl* self,
             StorageOptions storage_options,
             MissiveArgs::StorageParameters parameters,
             base::OnceCallback<void(StatusOr<scoped_refptr<StorageModule>>)>
                 callback)> create_storage_factory) {
  create_storage_factory_ = std::move(create_storage_factory);
  return *this;
}

void MissiveImpl::StartUp(scoped_refptr<dbus::Bus> bus,
                          feature::PlatformFeaturesInterface* feature_lib,
                          base::OnceCallback<void(Status)> cb) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  analytics::Metrics::Initialize();

  CHECK(upload_client_factory_) << "May be called only once";
  CHECK(create_storage_factory_) << "May be called only once";
  CHECK(!args_) << "Can only be called once";
  args_ = std::make_unique<SequencedMissiveArgs>(bus->GetDBusTaskRunner(),
                                                 feature_lib);

  // Migrate from /var/cache to /var/spool
  Status migration_status;
  std::tie(reporting_storage_dir_, migration_status) =
      Migrate(base::FilePath("/var/cache/reporting"),
              base::FilePath("/var/spool/reporting"));
  if (!migration_status.ok()) {
    LOG(ERROR) << migration_status.error_message();
  }
  // A safeguard: reporting_storage_dir_ must not be empty upon finishing
  // starting up.
  CHECK(!reporting_storage_dir_.empty());

  std::move(upload_client_factory_)
      .Run(bus, base::BindPostTaskToCurrentDefault(
                    base::BindOnce(&MissiveImpl::OnUploadClientCreated,
                                   GetWeakPtr(), std::move(cb))));
}

void MissiveImpl::OnUploadClientCreated(
    base::OnceCallback<void(Status)> cb,
    StatusOr<scoped_refptr<UploadClient>> upload_client_result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!upload_client_result.ok()) {
    std::move(cb).Run(upload_client_result.status());
    return;
  }
  upload_client_ = std::move(upload_client_result.ValueOrDie());

  // `GetCollectionParameters` only responds once the features were updated.
  args_->AsyncCall(&MissiveArgs::GetCollectionParameters)
      .WithArgs(base::BindPostTaskToCurrentDefault(base::BindOnce(
          &MissiveImpl::OnCollectionParameters, GetWeakPtr(), std::move(cb))));
}

void MissiveImpl::OnCollectionParameters(
    base::OnceCallback<void(Status)> cb,
    StatusOr<MissiveArgs::CollectionParameters> collection_parameters_result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!collection_parameters_result.ok()) {
    std::move(cb).Run(collection_parameters_result.status());
    return;
  }

  const auto& collection_parameters = collection_parameters_result.ValueOrDie();
  enqueuing_record_tallier_ = std::make_unique<EnqueuingRecordTallier>(
      collection_parameters.enqueuing_record_tallier);
  CHECK(!reporting_storage_dir_.empty())
      << "Reporting storage dir must have been set upon startup.";
  auto storage_collector =
      std::make_unique<analytics::ResourceCollectorStorage>(
          collection_parameters.storage_collector_interval,
          reporting_storage_dir_);
  storage_upload_success_cb_ = base::BindRepeating(
      &analytics::ResourceCollectorStorage::RecordUploadProgress,
      // ResourceCollectorStorage expected to outlive StorageModule that will
      // own this callback.
      base::Unretained(storage_collector.get()));
  analytics_registry_.Add("Storage", std::move(storage_collector));
  analytics_registry_.Add("CPU",
                          std::make_unique<analytics::ResourceCollectorCpu>(
                              collection_parameters.cpu_collector_interval));

  StorageOptions storage_options;
  storage_options.set_directory(reporting_storage_dir_)
      .set_signature_verification_public_key(
          SignatureVerifier::VerificationKey());
  auto memory_resource = storage_options.memory_resource();
  disk_space_resource_ = storage_options.disk_space_resource();
  analytics_registry_.Add("Memory",
                          std::make_unique<analytics::ResourceCollectorMemory>(
                              collection_parameters.memory_collector_interval,
                              std::move(memory_resource)));

  // `GetStorageParameters` only responds once the features were updated.
  args_->AsyncCall(&MissiveArgs::GetStorageParameters)
      .WithArgs(base::BindPostTaskToCurrentDefault(
          base::BindOnce(&MissiveImpl::OnStorageParameters, GetWeakPtr(),
                         std::move(cb), std::move(storage_options))));
}

void MissiveImpl::OnStorageParameters(
    base::OnceCallback<void(Status)> cb,
    StorageOptions storage_options,
    StatusOr<MissiveArgs::StorageParameters> storage_parameters_result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!storage_parameters_result.ok()) {
    std::move(cb).Run(storage_parameters_result.status());
    return;
  }
  auto& parameters = storage_parameters_result.ValueOrDie();

  // Create `Storage` service modules and register for dynamic update.
  queues_container_ =
      QueuesContainer::Create(parameters.controlled_degradation);
  compression_module_ = std::move(compression_module_factory_).Run(parameters);
  encryption_module_ = std::move(encryption_module_factory_).Run(parameters);
  signature_verification_dev_flag_ =
      base::MakeRefCounted<SignatureVerificationDevFlag>(
          parameters.signature_verification_dev_enabled);
  args_->AsyncCall(&MissiveArgs::OnStorageParametersUpdate)
      .WithArgs(base::BindPostTaskToCurrentDefault(base::BindRepeating(
                    &MissiveImpl::OnStorageParametersUpdate, GetWeakPtr())),
                base::DoNothing());
  health_module_ = std::move(health_module_factory_)
                       .Run(storage_options.directory().Append(
                           HealthModule::kHealthSubdirectory));

  std::move(create_storage_factory_)
      .Run(this, std::move(storage_options), std::move(parameters),
           base::BindPostTaskToCurrentDefault(
               base::BindOnce(&MissiveImpl::OnStorageModuleConfigured,
                              GetWeakPtr(), std::move(cb))));
}

Status MissiveImpl::ShutDown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return Status::StatusOK();
}

void MissiveImpl::CreateStorage(
    StorageOptions storage_options,
    MissiveArgs::StorageParameters parameters,
    base::OnceCallback<void(StatusOr<scoped_refptr<StorageModule>>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Create `Storage`.
  StorageModule::Create(
      {.options = std::move(storage_options),
       .legacy_storage_enabled = parameters.legacy_storage_enabled,
       .queues_container = queues_container_,
       .encryption_module = encryption_module_,
       .compression_module = compression_module_,
       .health_module = health_module_,
       .signature_verification_dev_flag = signature_verification_dev_flag_,
       .async_start_upload_cb = base::BindPostTaskToCurrentDefault(
           base::BindRepeating(&MissiveImpl::AsyncStartUpload, GetWeakPtr()))},
      std::move(callback));
}

// static
scoped_refptr<HealthModule> MissiveImpl::CreateHealthModule(
    const base::FilePath& file_path) {
  return HealthModule::Create(
      std::make_unique<HealthModuleDelegateImpl>(file_path));
}

// static
scoped_refptr<CompressionModule> MissiveImpl::CreateCompressionModule(
    const MissiveArgs::StorageParameters& parameters) {
  return CompressionModule::Create(parameters.compression_enabled,
                                   kCompressionThreshold, kCompressionType);
}

// static
scoped_refptr<EncryptionModuleInterface> MissiveImpl::CreateEncryptionModule(
    const MissiveArgs::StorageParameters& parameters) {
  return EncryptionModule::Create(parameters.encryption_enabled);
}

void MissiveImpl::OnStorageModuleConfigured(
    base::OnceCallback<void(Status)> cb,
    StatusOr<scoped_refptr<StorageModule>> storage_module_result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!storage_module_result.ok()) {
    std::move(cb).Run(storage_module_result.status());
    return;
  }
  storage_module_ = std::move(storage_module_result.ValueOrDie());
  storage_module_->AttachUploadSuccessCb(storage_upload_success_cb_);
  std::move(cb).Run(Status::StatusOK());
}

// static
void MissiveImpl::AsyncStartUpload(
    base::WeakPtr<MissiveImpl> missive,
    UploaderInterface::UploadReason reason,
    UploaderInterface::UploaderInterfaceResultCb uploader_result_cb) {
  if (!missive) {
    std::move(uploader_result_cb)
        .Run(Status(error::UNAVAILABLE, "Missive service has been shut down"));
    return;
  }
  missive->AsyncStartUploadInternal(reason, std::move(uploader_result_cb));
}

void MissiveImpl::AsyncStartUploadInternal(
    UploaderInterface::UploadReason reason,
    UploaderInterface::UploaderInterfaceResultCb uploader_result_cb) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(uploader_result_cb);
  if (!is_enabled_) {
    std::move(uploader_result_cb)
        .Run(Status(error::FAILED_PRECONDITION, "Reporting is disabled"));
    return;
  }
  if (!storage_module_) {
    // This is a precaution for a rare case - usually `storage_module_` is
    // already set by the time `AsyncStartUpload`.
    std::move(uploader_result_cb)
        .Run(Status(error::FAILED_PRECONDITION,
                    "Missive service not yet ready"));
    return;
  }
  CreateUploadJob(health_module_, reason, std::move(uploader_result_cb));
}

void MissiveImpl::CreateUploadJob(
    scoped_refptr<HealthModule> health_module,
    UploaderInterface::UploadReason reason,
    UploaderInterface::UploaderInterfaceResultCb uploader_result_cb) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto upload_job_result = UploadJob::Create(
      upload_client_,
      /*need_encryption_key=*/
      (encryption_module_->is_enabled() &&
       reason == UploaderInterface::UploadReason::KEY_DELIVERY),
      std::move(health_module),
      /*remaining_storage_capacity=*/disk_space_resource_->GetTotal() -
          disk_space_resource_->GetUsed(),
      /*new_events_rate=*/enqueuing_record_tallier_->GetAverage(),
      std::move(uploader_result_cb),
      base::BindPostTaskToCurrentDefault(
          base::BindOnce(&MissiveImpl::HandleUploadResponse, GetWeakPtr())));
  if (!upload_job_result.ok()) {
    // In the event that UploadJob::Create fails, it will call
    // |uploader_result_cb| with a failure status.
    LOG(ERROR) << "Was unable to create UploadJob, status:"
               << upload_job_result.status();
    return;
  }
  scheduler_.EnqueueJob(std::move(upload_job_result.ValueOrDie()));
}

void MissiveImpl::EnqueueRecord(
    const EnqueueRecordRequest& in_request,
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<EnqueueRecordResponse>>
        out_response) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!is_enabled_) {
    out_response->Return(RespondMissiveDisabled<EnqueueRecordResponse>());
    return;
  }
  if (!in_request.has_record()) {
    EnqueueRecordResponse response_body;
    auto* status = response_body.mutable_status();
    status->set_code(error::INVALID_ARGUMENT);
    status->set_error_message("Request had no Record");
    out_response->Return(response_body);
    return;
  }
  if (!in_request.has_priority()) {
    EnqueueRecordResponse response_body;
    auto* status = response_body.mutable_status();
    status->set_code(error::INVALID_ARGUMENT);
    status->set_error_message("Request had no Priority");
    out_response->Return(response_body);
    return;
  }

  // Tally the enqueuing record
  if (in_request.has_record()) {
    enqueuing_record_tallier_->Tally(in_request.record());
  }

  scheduler_.EnqueueJob(
      EnqueueJob::Create(storage_module_, health_module_, in_request,
                         std::make_unique<EnqueueJob::EnqueueResponseDelegate>(
                             health_module_, std::move(out_response))));
}

void MissiveImpl::FlushPriority(
    const FlushPriorityRequest& in_request,
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<FlushPriorityResponse>>
        out_response) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!is_enabled_) {
    out_response->Return(RespondMissiveDisabled<FlushPriorityResponse>());
    return;
  }

  if (in_request.has_health_data_logging_enabled()) {
    health_module_->set_debugging(in_request.health_data_logging_enabled());
  }

  FlushPriorityResponse response_body;
  storage_module_->Flush(
      in_request.priority(),
      base::BindPostTaskToCurrentDefault(base::BindOnce(
          &HandleResponse<FlushPriorityResponse>, std::move(out_response),
          std::move(response_body), health_module_)));
}

void MissiveImpl::ConfirmRecordUpload(
    const ConfirmRecordUploadRequest& in_request,
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<ConfirmRecordUploadResponse>>
        out_response) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!is_enabled_) {
    out_response->Return(RespondMissiveDisabled<ConfirmRecordUploadResponse>());
    return;
  }
  ConfirmRecordUploadResponse response_body;
  if (!in_request.has_sequence_information()) {
    auto* status = response_body.mutable_status();
    status->set_code(error::INVALID_ARGUMENT);
    status->set_error_message("Request had no SequenceInformation");
    out_response->Return(response_body);
    return;
  }

  if (in_request.has_health_data_logging_enabled()) {
    health_module_->set_debugging(in_request.health_data_logging_enabled());
  }

  storage_module_->ReportSuccess(
      in_request.sequence_information(), in_request.force_confirm(),
      base::BindPostTaskToCurrentDefault(base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 ConfirmRecordUploadResponse>> out_response,
             ConfirmRecordUploadResponse response_body,
             scoped_refptr<HealthModule> health_module, Status status) {
            LOG_IF(ERROR, !status.ok())
                << "Unable to confirm record deletion: " << status;
            HandleResponse<ConfirmRecordUploadResponse>(
                std::move(out_response), std::move(response_body),
                health_module, Status::StatusOK());
          },
          std::move(out_response), std::move(response_body), health_module_)));
}

void MissiveImpl::UpdateConfigInMissive(
    const UpdateConfigInMissiveRequest& in_request,
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<UpdateConfigInMissiveResponse>>
        out_response) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!is_enabled_) {
    out_response->Return(
        RespondMissiveDisabled<UpdateConfigInMissiveResponse>());
    return;
  }
  UpdateConfigInMissiveResponse response_body;
  if (!in_request.has_list_of_blocked_destinations()) {
    auto status = response_body.mutable_status();
    status->set_code(error::INVALID_ARGUMENT);
    status->set_error_message("Request had no ListOfBlockedDestinations");
    out_response->Return(response_body);
    return;
  }
  // Do nothing in the mean time.
  out_response->Return(response_body);
}

void MissiveImpl::UpdateEncryptionKey(
    const UpdateEncryptionKeyRequest& in_request,
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<UpdateEncryptionKeyResponse>>
        out_response) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!is_enabled_) {
    out_response->Return(RespondMissiveDisabled<UpdateEncryptionKeyResponse>());
    return;
  }
  UpdateEncryptionKeyResponse response_body;
  if (!in_request.has_signed_encryption_info()) {
    auto status = response_body.mutable_status();
    status->set_code(error::INVALID_ARGUMENT);
    status->set_error_message("Request had no SignedEncryptionInfo");
    out_response->Return(response_body);
    return;
  }

  storage_module_->UpdateEncryptionKey(in_request.signed_encryption_info());
  out_response->Return(response_body);
}

void MissiveImpl::HandleUploadResponse(
    StatusOr<UploadEncryptedRecordResponse> upload_response) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!upload_response.ok()) {
    return;  // No response received.
  }
  const auto& upload_response_value = upload_response.ValueOrDie();
  if (!upload_response_value.has_status()) {
    CHECK(!upload_response_value.disable())
        << "Cannot disable reporting, no error status";
    return;
  }
  if (upload_response_value.disable()) {
    // Disable reporting based on the response from Chrome.
    // Note: there is no way to re-enable it after that, because we do not talk
    // to it anymore.
    CHECK(upload_response_value.has_status())
        << "Disable signal should be accompanied by status";
    Status upload_status;
    upload_status.RestoreFrom(upload_response_value.status());
    LOG(ERROR) << "Disable reporting, status=" << upload_status;
    SetEnabled(/*is_enabled=*/false);
  }
  if (upload_response_value.has_health_data_logging_enabled()) {
    health_module_->set_debugging(
        upload_response_value.health_data_logging_enabled());
  }
}

void MissiveImpl::SetEnabled(bool is_enabled) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_enabled_ == is_enabled) {
    return;  // No change.
  }
  is_enabled_ = is_enabled;
  LOG(WARNING) << "Reporting is " << (is_enabled_ ? "enabled" : "disabled");
}

void MissiveImpl::OnStorageParametersUpdate(
    MissiveArgs::StorageParameters storage_parameters) {
  queues_container_->SetValue(storage_parameters.controlled_degradation);
  compression_module_->SetValue(storage_parameters.compression_enabled);
  encryption_module_->SetValue(storage_parameters.encryption_enabled);
  signature_verification_dev_flag_->SetValue(
      storage_parameters.signature_verification_dev_enabled);
  if (storage_module_) {
    storage_module_->SetLegacyEnabledPriorities(
        storage_parameters.legacy_storage_enabled);
  }
}

base::WeakPtr<MissiveImpl> MissiveImpl::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}
}  // namespace reporting
