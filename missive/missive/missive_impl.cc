// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/missive/missive_impl.h"

#include <cstdlib>
#include <string>
#include <utility>

#include <base/logging.h>
#include <base/time/time.h>

#include "missive/analytics/resource_collector_cpu.h"
#include "missive/analytics/resource_collector_memory.h"
#include "missive/analytics/resource_collector_storage.h"
#include "missive/compression/compression_module.h"
#include "missive/dbus/upload_client.h"
#include "missive/encryption/encryption_module.h"
#include "missive/encryption/verification.h"
#include "missive/missive/missive_args.h"
#include "missive/proto/interface.pb.h"
#include "missive/proto/record.pb.h"
#include "missive/scheduler/enqueue_job.h"
#include "missive/scheduler/scheduler.h"
#include "missive/scheduler/upload_job.h"
#include "missive/storage/storage_configuration.h"
#include "missive/storage/storage_module.h"
#include "missive/storage/storage_module_interface.h"
#include "missive/storage/storage_uploader_interface.h"
#include "missive/util/status.h"
#include "missive/util/statusor.h"

namespace reporting {

namespace {

constexpr char kReportingDirectory[] = "/var/cache/reporting";
constexpr CompressionInformation::CompressionAlgorithm kCompressionType =
    CompressionInformation::COMPRESSION_SNAPPY;
constexpr size_t kCompressionThreshold = 512U;

}  // namespace

MissiveImpl::MissiveImpl(
    std::unique_ptr<MissiveArgs> args,
    base::OnceCallback<
        void(scoped_refptr<dbus::Bus> bus,
             base::OnceCallback<void(StatusOr<scoped_refptr<UploadClient>>)>
                 callback)> upload_client_factory,
    base::OnceCallback<
        void(MissiveImpl* self,
             StorageOptions storage_options,
             base::OnceCallback<void(
                 StatusOr<scoped_refptr<StorageModuleInterface>>)> callback)>
        create_storage_factory)
    : args_(std::move(args)),
      upload_client_factory_(std::move(upload_client_factory)),
      create_storage_factory_(std::move(create_storage_factory)) {}

MissiveImpl::~MissiveImpl() = default;

void MissiveImpl::StartUp(scoped_refptr<dbus::Bus> bus,
                          base::OnceCallback<void(Status)> cb) {
  DCHECK(upload_client_factory_) << "May be called only once";
  DCHECK(create_storage_factory_) << "May be called only once";
  std::move(upload_client_factory_)
      .Run(bus, base::BindOnce(&MissiveImpl::OnUploadClientCreated,
                               base::Unretained(this), std::move(cb)));
}

void MissiveImpl::OnUploadClientCreated(
    base::OnceCallback<void(Status)> cb,
    StatusOr<scoped_refptr<UploadClient>> upload_client_result) {
  if (!upload_client_result.ok()) {
    std::move(cb).Run(upload_client_result.status());
    return;
  }
  upload_client_ = std::move(upload_client_result.ValueOrDie());
  enqueuing_record_tallier_ = std::make_unique<EnqueuingRecordTallier>(
      args_->enqueuing_record_tallier());
  const base::FilePath kReportingPath(kReportingDirectory);
  analytics_registry_.Add(
      "Storage", std::make_unique<analytics::ResourceCollectorStorage>(
                     args_->storage_collector_interval(), kReportingPath));
  analytics_registry_.Add("CPU",
                          std::make_unique<analytics::ResourceCollectorCpu>(
                              args_->cpu_collector_interval()));
  StorageOptions storage_options;
  storage_options.set_directory(kReportingPath)
      .set_signature_verification_public_key(
          SignatureVerifier::VerificationKey());
  auto memory_resource = storage_options.memory_resource();
  disk_space_resource_ = storage_options.disk_space_resource();
  analytics_registry_.Add(
      "Memory",
      std::make_unique<analytics::ResourceCollectorMemory>(
          args_->memory_collector_interval(), std::move(memory_resource)));
  std::move(create_storage_factory_)
      .Run(this, std::move(storage_options),
           base::BindOnce(&MissiveImpl::OnStorageModuleConfigured,
                          base::Unretained(this), std::move(cb)));
}

Status MissiveImpl::ShutDown() {
  return Status::StatusOK();
}

void MissiveImpl::CreateStorage(
    StorageOptions storage_options,
    base::OnceCallback<void(StatusOr<scoped_refptr<StorageModuleInterface>>)>
        callback) {
  StorageModule::Create(
      std::move(storage_options),
      base::BindRepeating(&MissiveImpl::AsyncStartUpload,
                          base::Unretained(this)),
      EncryptionModule::Create(),
      CompressionModule::Create(kCompressionThreshold, kCompressionType),
      std::move(callback));
}

void MissiveImpl::OnStorageModuleConfigured(
    base::OnceCallback<void(Status)> cb,
    StatusOr<scoped_refptr<StorageModuleInterface>> storage_module_result) {
  if (!storage_module_result.ok()) {
    std::move(cb).Run(storage_module_result.status());
    return;
  }
  storage_module_ = std::move(storage_module_result.ValueOrDie());
  std::move(cb).Run(Status::StatusOK());
}

void MissiveImpl::AsyncStartUpload(
    UploaderInterface::UploadReason reason,
    UploaderInterface::UploaderInterfaceResultCb uploader_result_cb) {
  DCHECK(uploader_result_cb);
  DCHECK(storage_module_);
  auto upload_job_result = UploadJob::Create(
      upload_client_,
      /*need_encryption_key=*/
      (EncryptionModuleInterface::is_enabled() &&
       reason == UploaderInterface::UploadReason::KEY_DELIVERY),
      /*remaining_storage_capacity=*/disk_space_resource_->GetTotal() -
          disk_space_resource_->GetUsed(),
      /*new_events_rate=*/enqueuing_record_tallier_->GetAverage(),
      std::move(uploader_result_cb));
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
  scoped_refptr<base::SequencedTaskRunner> task_runner =
      base::SequencedTaskRunnerHandle::Get();
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
      EnqueueJob::Create(storage_module_, in_request,
                         std::make_unique<EnqueueJob::EnqueueResponseDelegate>(
                             std::move(out_response))));
}

void MissiveImpl::FlushPriority(
    const FlushPriorityRequest& in_request,
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<FlushPriorityResponse>>
        out_response) {
  storage_module_->Flush(
      in_request.priority(),
      base::BindOnce(&MissiveImpl::HandleFlushResponse, base::Unretained(this),
                     std::move(out_response)));
}

void MissiveImpl::HandleFlushResponse(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        FlushPriorityResponse>> out_response,
    Status status) const {
  FlushPriorityResponse response_body;
  status.SaveTo(response_body.mutable_status());
  out_response->Return(response_body);
}

void MissiveImpl::ConfirmRecordUpload(
    const ConfirmRecordUploadRequest& in_request,
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<ConfirmRecordUploadResponse>>
        out_response) {
  ConfirmRecordUploadResponse response_body;
  if (!in_request.has_sequence_information()) {
    auto* status = response_body.mutable_status();
    status->set_code(error::INVALID_ARGUMENT);
    status->set_error_message("Request had no SequenceInformation");
    out_response->Return(response_body);
    return;
  }

  storage_module_->ReportSuccess(in_request.sequence_information(),
                                 in_request.force_confirm());
  out_response->Return(response_body);
}

void MissiveImpl::UpdateEncryptionKey(
    const UpdateEncryptionKeyRequest& in_request,
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<UpdateEncryptionKeyResponse>>
        out_response) {
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

}  // namespace reporting
