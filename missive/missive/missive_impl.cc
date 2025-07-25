// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/missive/missive_impl.h"

#include <cstdlib>
#include <list>
#include <tuple>
#include <utility>

#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/memory/scoped_refptr.h>
#include <base/task/bind_post_task.h>
#include <base/time/time.h>
#include <base/types/expected.h>
#include <featured/feature_library.h>

#include "base/functional/callback_forward.h"
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
#include "missive/scheduler/confirm_records_job.h"
#include "missive/scheduler/enqueue_job.h"
#include "missive/scheduler/flush_job.h"
#include "missive/scheduler/scheduler.h"
#include "missive/scheduler/update_config_job.h"
#include "missive/scheduler/update_key_job.h"
#include "missive/scheduler/upload_job.h"
#include "missive/storage/storage_configuration.h"
#include "missive/storage/storage_module.h"
#include "missive/storage/storage_uploader_interface.h"
#include "missive/util/reporting_errors.h"
#include "missive/util/status.h"
#include "missive/util/statusor.h"

namespace reporting {

namespace {

constexpr CompressionInformation::CompressionAlgorithm kCompressionType =
    CompressionInformation::COMPRESSION_SNAPPY;
constexpr size_t kCompressionThreshold = 512U;

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
MissiveImpl& MissiveImpl::SetServerConfigurationControllerFactory(
    base::OnceCallback<scoped_refptr<ServerConfigurationController>(
        const MissiveArgs::ConfigFileParameters& parameters)>
        server_configuration_controller_factory) {
  server_configuration_controller_factory_ =
      std::move(server_configuration_controller_factory);
  return *this;
}
MissiveImpl& MissiveImpl::SetStorageModuleFactory(
    base::OnceCallback<
        void(MissiveImpl* self,
             StorageOptions storage_options,
             MissiveArgs::StorageParameters parameters,
             base::RepeatingClosure storage_upload_success_cb,
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
  if (!upload_client_result.has_value()) {
    std::move(cb).Run(upload_client_result.error());
    return;
  }
  upload_client_ = std::move(upload_client_result.value());

  // `GetConfigFileParameters` only responds once the features were updated.
  args_->AsyncCall(&MissiveArgs::GetConfigFileParameters)
      .WithArgs(base::BindPostTaskToCurrentDefault(base::BindOnce(
          &MissiveImpl::OnConfigFileParameters, GetWeakPtr(), std::move(cb))));
}

void MissiveImpl::OnConfigFileParameters(
    base::OnceCallback<void(Status)> cb,
    StatusOr<MissiveArgs::ConfigFileParameters> config_file_parameters_result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!config_file_parameters_result.has_value()) {
    std::move(cb).Run(config_file_parameters_result.error());
    return;
  }

  auto& parameters = config_file_parameters_result.value();
  server_configuration_controller_ =
      std::move(server_configuration_controller_factory_).Run(parameters);

  // Register for dynamic updates on the flags.
  args_->AsyncCall(&MissiveArgs::OnConfigFileParametersUpdate)
      .WithArgs(base::BindPostTaskToCurrentDefault(base::BindRepeating(
                    &MissiveImpl::OnConfigFileParametersUpdate, GetWeakPtr())),
                base::DoNothing());

  // `GetCollectionParameters` only responds once the features were updated.
  args_->AsyncCall(&MissiveArgs::GetCollectionParameters)
      .WithArgs(base::BindPostTaskToCurrentDefault(base::BindOnce(
          &MissiveImpl::OnCollectionParameters, GetWeakPtr(), std::move(cb))));
}

void MissiveImpl::OnCollectionParameters(
    base::OnceCallback<void(Status)> cb,
    StatusOr<MissiveArgs::CollectionParameters> collection_parameters_result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!collection_parameters_result.has_value()) {
    std::move(cb).Run(collection_parameters_result.error());
    return;
  }

  const auto& collection_parameters = collection_parameters_result.value();
  enqueuing_record_tallier_ = std::make_unique<EnqueuingRecordTallier>(
      collection_parameters.enqueuing_record_tallier);
  CHECK(!reporting_storage_dir_.empty())
      << "Reporting storage dir must have been set upon startup.";
  auto storage_collector =
      std::make_unique<analytics::ResourceCollectorStorage>(
          collection_parameters.storage_collector_interval,
          reporting_storage_dir_);
  auto storage_upload_success_cb =
      base::BindPostTaskToCurrentDefault(base::BindRepeating(
          &analytics::ResourceCollectorStorage::RecordUploadProgress,
          storage_collector->GetWeakPtr()));
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
      .WithArgs(base::BindPostTaskToCurrentDefault(base::BindOnce(
          &MissiveImpl::OnStorageParameters, GetWeakPtr(), std::move(cb),
          std::move(storage_options), std::move(storage_upload_success_cb))));
}

void MissiveImpl::OnStorageParameters(
    base::OnceCallback<void(Status)> cb,
    StorageOptions storage_options,
    base::RepeatingClosure storage_upload_success_cb,
    StatusOr<MissiveArgs::StorageParameters> storage_parameters_result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!storage_parameters_result.has_value()) {
    std::move(cb).Run(storage_parameters_result.error());
    return;
  }
  auto& parameters = storage_parameters_result.value();

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
           std::move(storage_upload_success_cb),
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
    base::RepeatingClosure storage_upload_success_cb,
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
       .server_configuration_controller = server_configuration_controller_,
       .signature_verification_dev_flag = signature_verification_dev_flag_,
       .storage_upload_success_cb = std::move(storage_upload_success_cb),
       .async_start_upload_cb = base::BindPostTaskToCurrentDefault(
           base::BindRepeating(&MissiveImpl::AsyncStartUpload, GetWeakPtr()))},
      std::move(callback));
}

// static
scoped_refptr<ServerConfigurationController>
MissiveImpl::CreateServerConfigurationController(
    const MissiveArgs::ConfigFileParameters& parameters) {
  return ServerConfigurationController::Create(
      parameters.blocking_destinations_enabled);
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
  if (!storage_module_result.has_value()) {
    std::move(cb).Run(storage_module_result.error());
    return;
  }
  storage_module_ = std::move(storage_module_result.value());
  std::move(cb).Run(Status::StatusOK());
}

// static
void MissiveImpl::AsyncStartUpload(
    base::WeakPtr<MissiveImpl> missive,
    UploaderInterface::UploadReason reason,
    UploaderInterface::InformAboutCachedUploadsCb inform_cb,
    UploaderInterface::UploaderInterfaceResultCb uploader_result_cb) {
  if (!missive) {
    std::move(uploader_result_cb)
        .Run(base::unexpected(
            Status(error::UNAVAILABLE, "Missive service has been shut down")));

    analytics::Metrics::SendEnumToUMA(
        kUmaUnavailableErrorReason,
        UnavailableErrorReason::MISSIVE_HAS_BEEN_SHUT_DOWN,
        UnavailableErrorReason::MAX_VALUE);
    return;
  }
  missive->AsyncStartUploadInternal(reason, std::move(inform_cb),
                                    std::move(uploader_result_cb));
}

void MissiveImpl::AsyncStartUploadInternal(
    UploaderInterface::UploadReason reason,
    UploaderInterface::InformAboutCachedUploadsCb inform_cb,
    UploaderInterface::UploaderInterfaceResultCb uploader_result_cb) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(uploader_result_cb);
  if (!is_enabled_) {
    std::move(uploader_result_cb)
        .Run(base::unexpected(
            Status(error::FAILED_PRECONDITION, "Reporting is disabled")));
    return;
  }
  if (!storage_module_) {
    // This is a precaution for a rare case - usually `storage_module_` is
    // already set by the time `AsyncStartUpload`.
    std::move(uploader_result_cb)
        .Run(base::unexpected(Status(error::FAILED_PRECONDITION,
                                     "Missive service not yet ready")));
    return;
  }
  CreateUploadJob(health_module_, reason, std::move(inform_cb),
                  std::move(uploader_result_cb));
}

void MissiveImpl::CreateUploadJob(
    scoped_refptr<HealthModule> health_module,
    UploaderInterface::UploadReason reason,
    UploaderInterface::InformAboutCachedUploadsCb inform_cb,
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
          base::BindOnce(&MissiveImpl::HandleUploadResponse, GetWeakPtr(),
                         std::move(inform_cb))));
  if (!upload_job_result.has_value()) {
    // In the event that UploadJob::Create fails, it will call
    // |uploader_result_cb| with a failure status.
    LOG(ERROR) << "Was unable to create UploadJob, status:"
               << upload_job_result.error();
    return;
  }
  scheduler_.EnqueueJob(std::move(upload_job_result.value()));
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

  scheduler_.EnqueueJob(
      FlushJob::Create(storage_module_, health_module_, in_request,
                       std::make_unique<FlushJob::FlushResponseDelegate>(
                           health_module_, std::move(out_response))));
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

  if (in_request.has_health_data_logging_enabled()) {
    health_module_->set_debugging(in_request.health_data_logging_enabled());
  }

  scheduler_.EnqueueJob(ConfirmRecordsJob::Create(
      storage_module_, health_module_, in_request,
      std::make_unique<ConfirmRecordsJob::ConfirmRecordsResponseDelegate>(
          health_module_, std::move(out_response))));
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

  scheduler_.EnqueueJob(UpdateConfigInMissiveJob::Create(
      health_module_, server_configuration_controller_, in_request,
      std::make_unique<
          UpdateConfigInMissiveJob::UpdateConfigInMissiveResponseDelegate>(
          std::move(out_response))));
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

  scheduler_.EnqueueJob(UpdateEncryptionKeyJob::Create(
      storage_module_, in_request,
      std::make_unique<
          UpdateEncryptionKeyJob::UpdateEncryptionKeyResponseDelegate>(
          std::move(out_response))));
}

void MissiveImpl::HandleUploadResponse(
    UploaderInterface::InformAboutCachedUploadsCb inform_cb,
    StatusOr<UploadEncryptedRecordResponse> upload_response) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!upload_response.has_value()) {
    return;  // No response received.
  }
  const auto& upload_response_value = upload_response.value();
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
  // Inform storage about events cached by uploader.
  std::list<int64_t> cached_events_seq_ids;
  for (const auto& seq_id : upload_response_value.cached_events_seq_ids()) {
    cached_events_seq_ids.emplace_back(seq_id);
  }
  std::move(inform_cb).Run(std::move(cached_events_seq_ids),
                           base::DoNothing());  // Do not wait!
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
    base::WeakPtr<MissiveImpl> self,
    MissiveArgs::StorageParameters storage_parameters) {
  if (!self) {
    return;
  }
  self->queues_container_->SetValue(storage_parameters.controlled_degradation);
  self->compression_module_->SetValue(storage_parameters.compression_enabled);
  self->encryption_module_->SetValue(storage_parameters.encryption_enabled);
  self->signature_verification_dev_flag_->SetValue(
      storage_parameters.signature_verification_dev_enabled);
  if (self->storage_module_) {
    self->storage_module_->SetLegacyEnabledPriorities(
        storage_parameters.legacy_storage_enabled);
  }
}

void MissiveImpl::OnConfigFileParametersUpdate(
    base::WeakPtr<MissiveImpl> self,
    MissiveArgs::ConfigFileParameters config_file_parameters) {
  if (!self) {
    return;
  }
  self->server_configuration_controller_->SetValue(
      config_file_parameters.blocking_destinations_enabled);
}

base::WeakPtr<MissiveImpl> MissiveImpl::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}
}  // namespace reporting
